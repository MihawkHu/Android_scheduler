/* drivers/misc/qemupipe/qemu_pipe.c
 *
 * Copyright (C) 2011 Google, Inc.
 *
 * This software is licensed under the terms of the GNU General Public
 * License version 2, as published by the Free Software Foundation, and
 * may be copied, distributed, and modified under those terms.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 */

/* This source file contains the implementation of a special device driver
 * that intends to provide a *very* fast communication channel between the
 * guest system and the QEMU emulator.
 *
 * Usage from the guest is simply the following (error handling simplified):
 *
 *    int  fd = open("/dev/qemu_pipe",O_RDWR);
 *    .... write() or read() through the pipe.
 *
 * This driver doesn't deal with the exact protocol used during the session.
 * It is intended to be as simple as something like:
 *
 *    // do this _just_ after opening the fd to connect to a specific
 *    // emulator service.
 *    const char*  msg = "<pipename>";
 *    if (write(fd, msg, strlen(msg)+1) < 0) {
 *       ... could not connect to <pipename> service
 *       close(fd);
 *    }
 *
 *    // after this, simply read() and write() to communicate with the
 *    // service. Exact protocol details left as an exercise to the reader.
 *
 * This driver is very fast because it doesn't copy any data through
 * intermediate buffers, since the emulator is capable of translating
 * guest user addresses into host ones.
 *
 * Note that we must however ensure that each user page involved in the
 * exchange is properly mapped during a transfer.
 */

#include <linux/module.h>
#include <linux/interrupt.h>
#include <linux/kernel.h>
#include <linux/spinlock.h>
#include <linux/miscdevice.h>
#include <linux/platform_device.h>
#include <linux/poll.h>
#include <linux/radix-tree.h>
#include <linux/sched.h>
#include <linux/bitops.h>
#include <linux/io.h>
#include <linux/slab.h>
#include <linux/mm.h>

/* Set to 1 for normal debugging, and 2 for extensive one */
#define PIPE_DEBUG  0

#define PIPE_E(...) printk(KERN_ERR "QEMU Pipe Device:"  __VA_ARGS__)
#define PIPE_W(...) printk(KERN_WARNING "QEMU Pipe Device:"  __VA_ARGS__)

#if PIPE_DEBUG >= 1
#  define  PIPE_D(...)  printk(KERN_INFO "QEMU Pipe Device:"  __VA_ARGS__)
#else
#  define  PIPE_D(...)  do {} while (0)
#endif

#if PIPE_DEBUG >= 2
#  define  PIPE_DD(...)  printk(KERN_INFO "QEMU Pipe Device:" __VA_ARGS__)
#else
#  define  PIPE_DD(...)  do {} while (0)
#endif

/* IMPORTANT: The following constants must match the ones used and defined
 * in external/qemu/hw/goldfish_pipe.c in the Android source tree.
 */

/* pipe device registers */
#define PIPE_REG_COMMAND            0x00  /* write: value = command */
#define PIPE_REG_STATUS             0x04  /* read */
#define PIPE_REG_CHANNEL            0x08  /* read/write: channel id */
#define PIPE_REG_SIZE               0x0c  /* read/write: buffer size */
#define PIPE_REG_ADDRESS            0x10  /* write: physical address */
#define PIPE_REG_WAKES              0x14  /* read: wake flags */
#define PIPE_REG_PARAMS_ADDR_LOW    0x18  /* read/write: batch data address */
#define PIPE_REG_PARAMS_ADDR_HIGH   0x1c  /* read/write: batch data address */
#define PIPE_REG_ACCESS_PARAMS      0x20  /* write: batch access */
#define PIPE_REG_VERSION            0x24  /* read: device version */

/* list of commands for PIPE_REG_COMMAND */
#define CMD_OPEN               1  /* open new channel */
#define CMD_CLOSE              2  /* close channel (from guest) */
#define CMD_POLL               3  /* poll read/write status */

/* List of bitflags returned in status of CMD_POLL command */
#define PIPE_POLL_IN   (1 << 0)
#define PIPE_POLL_OUT  (1 << 1)
#define PIPE_POLL_HUP  (1 << 2)

/* The following commands are related to write operations */
#define CMD_WRITE_BUFFER       4  /* send a user buffer to the emulator */
#define CMD_WAKE_ON_WRITE      5  /* tell the emulator to wake us when writing
				     is possible */

/* The following commands are related to read operations, they must be
 * listed in the same order than the corresponding write ones, since we
 * will use (CMD_READ_BUFFER - CMD_WRITE_BUFFER) as a special offset
 * in qemu_pipe_read_write() below.
 */
#define CMD_READ_BUFFER        6  /* receive a user buffer from the emulator */
#define CMD_WAKE_ON_READ       7  /* tell the emulator to wake us when reading
				   * is possible */

/* Possible status values used to signal errors - see qemu_pipe_error_convert */
#define PIPE_ERROR_INVAL       -1
#define PIPE_ERROR_AGAIN       -2
#define PIPE_ERROR_NOMEM       -3
#define PIPE_ERROR_IO          -4

/* Bit-flags used to signal events from the emulator */
#define PIPE_WAKE_CLOSED       (1 << 0)  /* emulator closed pipe */
#define PIPE_WAKE_READ         (1 << 1)  /* pipe can now be read from */
#define PIPE_WAKE_WRITE        (1 << 2)  /* pipe can now be written to */

struct access_params{
	uint32_t channel;
	uint32_t size;
	uint32_t address;
	uint32_t cmd;
	uint32_t result;
	/* reserved for future extension */
	uint32_t flags;
};

/* The global driver data. Holds a reference to the i/o page used to
 * communicate with the emulator, and a wake queue for blocked tasks
 * waiting to be awoken.
 */
struct qemu_pipe_dev {
	spinlock_t lock;
	unsigned char __iomem *base;
	struct access_params *aps;
	int irq;
	struct radix_tree_root pipes;
	u32 version;
};

static struct qemu_pipe_dev   pipe_dev[1];

/* This data type models a given pipe instance */
struct qemu_pipe {
	struct qemu_pipe_dev *dev;
	struct mutex lock;
	unsigned long flags;
	wait_queue_head_t wake_queue;
};


/* Bit flags for the 'flags' field */
enum {
	BIT_CLOSED_ON_HOST = 0,  /* pipe closed by host */
	BIT_WAKE_ON_WRITE  = 1,  /* want to be waked on writes */
	BIT_WAKE_ON_READ   = 2,  /* want to be waked on reads */
};

/* This function converts an error code returned by the emulator through
 * the PIPE_REG_STATUS i/o register into a valid negative errno value.
 */
static int qemu_pipe_error_convert(int status)
{
	switch (status) {
	case PIPE_ERROR_AGAIN:
		status = -EAGAIN; break;
	case PIPE_ERROR_NOMEM:
		status = -ENOMEM; break;
	case PIPE_ERROR_IO:
		status = -EIO; break;
	default:
		status = -EINVAL;
	}
	return status;
}

/*
 * Notice: QEMU will return 0 for unknown register access, indicating
 * param_acess is not supported
 */
static int valid_batchbuffer_addr(struct qemu_pipe_dev *dev,
				  struct access_params *aps)
{
	uint32_t aph = readl(dev->base + PIPE_REG_PARAMS_ADDR_HIGH);
	uint32_t apl = readl(dev->base + PIPE_REG_PARAMS_ADDR_LOW);
	uint64_t paddr = ((uint64_t)aph << 32) | apl;

	return paddr == (__pa(aps));
}

/* 0 on success */
static int setup_access_params_addr(struct qemu_pipe_dev *dev)
{
	uint64_t paddr;
	struct access_params *aps;

	aps = kmalloc(sizeof(struct access_params), GFP_KERNEL);
	if (!aps)
		return -1;

	paddr = __pa(aps);
	writel((uint32_t)(paddr >> 32), dev->base + PIPE_REG_PARAMS_ADDR_HIGH);
	writel((uint32_t)paddr, dev->base + PIPE_REG_PARAMS_ADDR_LOW);

	if (!valid_batchbuffer_addr(dev, aps))
		return -1;

	dev->aps = aps;
	return 0;
}

/* A value that will not be set by qemu emulator */
#define IMPOSSIBLE_BATCH_RESULT (0xdeadbeaf)

static int access_with_param(struct qemu_pipe_dev *dev, const int cmd,
			     unsigned long address, unsigned long avail,
			     struct qemu_pipe *pipe, int *status)
{
	struct access_params *aps = dev->aps;

	aps->result = IMPOSSIBLE_BATCH_RESULT;
	aps->channel = (unsigned long)pipe;
	aps->size = avail;
	aps->address = address;
	aps->cmd = cmd;
	writel(cmd, dev->base + PIPE_REG_ACCESS_PARAMS);

	/* If aps->result unchanged, then batch command failed */
	if (aps->result == IMPOSSIBLE_BATCH_RESULT)
		return -1;

	*status = aps->result;
	return 0;
}

/* This function is used for both reading from and writing to a given
 * pipe.
 */
static ssize_t qemu_pipe_read_write(struct file *filp, char __user *buffer,
				    size_t bufflen, int is_write)
{
	unsigned long irq_flags;
	struct qemu_pipe *pipe = filp->private_data;
	struct qemu_pipe_dev *dev = pipe->dev;
	const int cmd_offset = is_write ? 0
					: (CMD_READ_BUFFER - CMD_WRITE_BUFFER);
	unsigned long address, address_end;
	int count = 0, ret = -EINVAL;

	/* If the emulator already closed the pipe, no need to go further */
	if (test_bit(BIT_CLOSED_ON_HOST, &pipe->flags)) {
		PIPE_W("(write=%d) already closed!\n", is_write);
		ret = -EIO;
		goto out;
	}

	/* Null reads or writes succeeds */
	if (unlikely(bufflen) == 0)
		goto out;

	/* Check the buffer range for access */
	if (!access_ok(is_write ? VERIFY_WRITE : VERIFY_READ,
			buffer, bufflen)) {
		ret = -EFAULT;
		PIPE_W("rw access_ok failed\n");
		goto out;
	}

	/* Serialize access to the pipe */
	if (mutex_lock_interruptible(&pipe->lock)) {
		PIPE_W("(write=%d) interrupted!\n", is_write);
		return -ERESTARTSYS;
	}

	address = (unsigned long)(void *)buffer;
	address_end = address + bufflen;

	while (address < address_end) {
		unsigned long page_end = (address & PAGE_MASK) + PAGE_SIZE;
		unsigned long next     = page_end < address_end ? page_end
								: address_end;
		unsigned long avail    = next - address;
		int status, wakeBit;
		struct page *page;

		/* Either vaddr or paddr depending on the device version */
		unsigned long xaddr;

		/*
		 * We grab the pages on a page-by-page basis in case user
		 * space gives us a potentially huge buffer but the read only
		 * returns a small amount, then there's no need to pin that
		 * much memory to the process.
		 */
		down_read(&current->mm->mmap_sem);
		ret = get_user_pages(current, current->mm, address, 1,
				     !is_write, 0, &page, NULL);
		up_read(&current->mm->mmap_sem);
		if (ret < 0)
			return ret;

		if (dev->version) {
			/* Device version 1 or newer
			 * expects the physical address.
			 */
			xaddr = page_to_phys(page) | (address & ~PAGE_MASK);
		} else {
			/* Device version 0 expects the
			 * virtual address.
			 */
			xaddr = address;
		}

		/* Now, try to transfer the bytes in the current page */
		spin_lock_irqsave(&dev->lock, irq_flags);
		if (dev->aps == NULL || access_with_param(
			dev, CMD_WRITE_BUFFER + cmd_offset, xaddr, avail,
			pipe, &status) < 0)
		{
			writel((unsigned long)pipe,
				dev->base + PIPE_REG_CHANNEL);
			writel(avail, dev->base + PIPE_REG_SIZE);
			writel(xaddr, dev->base + PIPE_REG_ADDRESS);
			writel(CMD_WRITE_BUFFER + cmd_offset,
				dev->base + PIPE_REG_COMMAND);
			status = readl(dev->base + PIPE_REG_STATUS);
		}
		spin_unlock_irqrestore(&dev->lock, irq_flags);

		if (status > 0 && !is_write)
			set_page_dirty(page);
		put_page(page);

		if (status > 0) { /* Correct transfer */
			count += status;
			address += status;
			continue;
		} else if (status == 0) { /* EOF */
			ret = 0;
			break;
		} else if (status < 0 && count > 0) {
			/*
			 * An error occured and we already transfered
			 * something on one of the previous pages.
			 * Just return what we already copied and log this
			 * err.
			 *
			 * Note: This seems like an incorrect approach but
			 * cannot change it until we check if any user space
			 * ABI relies on this behavior.
			 */
			if (status != PIPE_ERROR_AGAIN)
				PIPE_W("goldfish_pipe: backend returned error %d on %s\n",
					    status, is_write ? "write" : "read");
			ret = 0;
			break;
		}

		/* If the error is not PIPE_ERROR_AGAIN, or if we are not in
		* non-blocking mode, just return the error code.
		*/
		if (status != PIPE_ERROR_AGAIN ||
			(filp->f_flags & O_NONBLOCK) != 0) {
			ret = qemu_pipe_error_convert(status);
			break;
		}

		/* We will have to wait until more data/space is available.
		* First, mark the pipe as waiting for a specific wake signal.
		*/
		wakeBit = is_write ? BIT_WAKE_ON_WRITE : BIT_WAKE_ON_READ;
		set_bit(wakeBit, &pipe->flags);

		/* Tell the emulator we're going to wait for a wake event */
		spin_lock_irqsave(&dev->lock, irq_flags);
		writel((unsigned long)pipe, dev->base + PIPE_REG_CHANNEL);
		writel(CMD_WAKE_ON_WRITE + cmd_offset,
			dev->base + PIPE_REG_COMMAND);
		spin_unlock_irqrestore(&dev->lock, irq_flags);

		/* Unlock the pipe, then wait for the wake signal */
		mutex_unlock(&pipe->lock);

		while (test_bit(wakeBit, &pipe->flags)) {
			if (wait_event_interruptible(
					pipe->wake_queue,
					!test_bit(wakeBit, &pipe->flags))) {
				ret = -ERESTARTSYS;
				PIPE_W("rw, wait_event error\n");
				goto out;
			}

			if (test_bit(BIT_CLOSED_ON_HOST, &pipe->flags)) {
				ret = -EIO;
				PIPE_W("rw, pipe already closed\n");
				goto out;
			}
		}

		/* Try to re-acquire the lock */
		if (mutex_lock_interruptible(&pipe->lock)) {
			ret = -ERESTARTSYS;
			break;
		}
	}
	mutex_unlock(&pipe->lock);
out:
	if (ret < 0)
		return ret;
	else
		return count;
}

static ssize_t qemu_pipe_read(struct file *filp, char __user *buffer,
			      size_t bufflen, loff_t *ppos)
{
	return qemu_pipe_read_write(filp, buffer, bufflen, 0);
}

static ssize_t qemu_pipe_write(struct file *filp,
				const char __user *buffer, size_t bufflen,
				loff_t *ppos)
{
	return qemu_pipe_read_write(filp, (char __user *)buffer, bufflen, 1);
}


static unsigned int qemu_pipe_poll(struct file *filp, poll_table *wait)
{
	struct qemu_pipe *pipe = filp->private_data;
	struct qemu_pipe_dev *dev = pipe->dev;
	unsigned long irq_flags;
	unsigned int mask = 0;
	int status;

	mutex_lock(&pipe->lock);

	poll_wait(filp, &pipe->wake_queue, wait);

	spin_lock_irqsave(&dev->lock, irq_flags);
	writel((unsigned long)pipe, dev->base + PIPE_REG_CHANNEL);
	writel(CMD_POLL, dev->base + PIPE_REG_COMMAND);
	status = readl(dev->base + PIPE_REG_STATUS);
	spin_unlock_irqrestore(&dev->lock, irq_flags);

	mutex_unlock(&pipe->lock);

	if (status & PIPE_POLL_IN)
		mask |= POLLIN | POLLRDNORM;

	if (status & PIPE_POLL_OUT)
		mask |= POLLOUT | POLLWRNORM;

	if (status & PIPE_POLL_HUP)
		mask |= POLLHUP;

	if (test_bit(BIT_CLOSED_ON_HOST, &pipe->flags))
		mask |= POLLERR;

	return mask;
}

static irqreturn_t qemu_pipe_interrupt(int irq, void *dev_id)
{
	struct qemu_pipe_dev *dev = dev_id;
	unsigned long irq_flags;
	int count = 0;

	/* We're going to read from the emulator a list of (channel,flags)
	* pairs corresponding to the wake events that occured on each
	* blocked pipe (i.e. channel).
	*/
	spin_lock_irqsave(&dev->lock, irq_flags);
	for (;;) {
		/* First read the channel, 0 means the end of the list */
		struct qemu_pipe *pipe;
		unsigned long wakes;
		unsigned long channel = readl(dev->base + PIPE_REG_CHANNEL);

		if (channel == 0)
			break;

		/* Convert channel to struct pipe pointer + read wake flags */
		wakes = readl(dev->base + PIPE_REG_WAKES);
		pipe  = (struct qemu_pipe *)(ptrdiff_t)channel;

		/* check if pipe is still valid */
		if ((pipe = radix_tree_lookup(&dev->pipes,
			(unsigned long)pipe)) == NULL) {
			PIPE_W("interrupt for already closed pipe\n");
			break;
		}
		/* Did the emulator just closed a pipe? */
		if (wakes & PIPE_WAKE_CLOSED) {
			set_bit(BIT_CLOSED_ON_HOST, &pipe->flags);
			wakes |= PIPE_WAKE_READ | PIPE_WAKE_WRITE;
		}
		if (wakes & PIPE_WAKE_READ)
			clear_bit(BIT_WAKE_ON_READ, &pipe->flags);
		if (wakes & PIPE_WAKE_WRITE)
			clear_bit(BIT_WAKE_ON_WRITE, &pipe->flags);

		wake_up_interruptible(&pipe->wake_queue);
		count++;
	}
	spin_unlock_irqrestore(&dev->lock, irq_flags);

	return (count == 0) ? IRQ_NONE : IRQ_HANDLED;
}

static int qemu_pipe_open(struct inode *inode, struct file *file)
{
	unsigned long irq_flags;
	struct qemu_pipe *pipe;
	struct qemu_pipe_dev *dev = pipe_dev;
	int32_t status;
	int ret;

	/* Allocate new pipe kernel object */
	pipe = kzalloc(sizeof(*pipe), GFP_KERNEL);
	if (pipe == NULL) {
		PIPE_E("Not enough kernel memory to allocate new pipe\n");
		return -ENOMEM;
	}

	PIPE_D("Opening pipe %p\n", pipe);

	pipe->dev = dev;
	mutex_init(&pipe->lock);
	init_waitqueue_head(&pipe->wake_queue);

	/* Now, tell the emulator we're opening a new pipe. We use the
	* pipe object's address as the channel identifier for simplicity.
	*/
	spin_lock_irqsave(&dev->lock, irq_flags);
	if ((ret = radix_tree_insert(&dev->pipes, (unsigned long)pipe, pipe))) {
		spin_unlock_irqrestore(&dev->lock, irq_flags);
		PIPE_E("opening pipe failed due to radix tree insertion failure\n");
		kfree(pipe);
		return ret;
	}
	writel((unsigned long)pipe, dev->base + PIPE_REG_CHANNEL);
	writel(CMD_OPEN, dev->base + PIPE_REG_COMMAND);
	status = readl(dev->base + PIPE_REG_STATUS);
	spin_unlock_irqrestore(&dev->lock, irq_flags);

	if (status < 0) {
		PIPE_E("Could not open pipe channel, error=%d\n", status);
		kfree(pipe);
		return status;
	}

	/* All is done, save the pipe into the file's private data field */
	file->private_data = pipe;
	return 0;
}

static int qemu_pipe_release(struct inode *inode, struct file *filp)
{
	unsigned long irq_flags;
	struct qemu_pipe *pipe = filp->private_data;
	struct qemu_pipe_dev *dev = pipe->dev;

	PIPE_D("Closing pipe %p\n", dev);

	/* The guest is closing the channel, so tell the emulator right now */
	spin_lock_irqsave(&dev->lock, irq_flags);
	writel((unsigned long)pipe, dev->base + PIPE_REG_CHANNEL);
	writel(CMD_CLOSE, dev->base + PIPE_REG_COMMAND);
	filp->private_data = NULL;
	radix_tree_delete(&pipe_dev->pipes, (unsigned long)pipe);
	kfree(pipe);
	spin_unlock_irqrestore(&dev->lock, irq_flags);

	return 0;
}

static const struct file_operations qemu_pipe_fops = {
	.owner = THIS_MODULE,
	.read = qemu_pipe_read,
	.write = qemu_pipe_write,
	.poll = qemu_pipe_poll,
	.open = qemu_pipe_open,
	.release = qemu_pipe_release,
};

static struct miscdevice qemu_pipe_device = {
	.minor = MISC_DYNAMIC_MINOR,
	.name = "qemu_pipe",
	.fops = &qemu_pipe_fops,
};

static int qemu_pipe_probe(struct platform_device *pdev)
{
	int err;
	struct resource *r;
	struct qemu_pipe_dev *dev = pipe_dev;

	PIPE_D("Creating device\n");

	INIT_RADIX_TREE(&dev->pipes, GFP_ATOMIC);
	/* not thread safe, but this should not happen */
	if (dev->base != NULL) {
		printk(KERN_ERR "QEMU PIPE Device: already mapped at %p\n",
			dev->base);
		return -ENODEV;
	}
	r = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	if (r == NULL || r->end - r->start < PAGE_SIZE - 1) {
		printk(KERN_ERR "QEMU PIPE Device: can't allocate i/o page\n");
		return -EINVAL;
	}
	dev->base = ioremap(r->start, PAGE_SIZE);
	PIPE_D("The mapped IO base is %p\n", dev->base);

	r = platform_get_resource(pdev, IORESOURCE_IRQ, 0);
	if (r == NULL) {
		printk(KERN_ERR "QEMU PIPE Device: failure to allocate IRQ\n");
		err = -EINVAL;
		goto err_alloc_irq;
	}
	dev->irq = r->start;
	PIPE_D("The IRQ is %d\n", dev->irq);
	err = request_irq(dev->irq, qemu_pipe_interrupt, IRQF_SHARED,
				"goldfish_pipe", dev);
	if (err)
		goto err_alloc_irq;

	spin_lock_init(&dev->lock);

	err = misc_register(&qemu_pipe_device);
	if (err)
		goto err_misc_register;

	setup_access_params_addr(dev);

	/* Acquire PipeDevice version information */
	dev->version = readl(dev->base + PIPE_REG_VERSION);
	return 0;

err_misc_register:
	free_irq(dev->irq, pdev);
err_alloc_irq:
	iounmap(dev->base);
	dev->base = NULL;
	return err;
}

static int qemu_pipe_remove(struct platform_device *pdev)
{
	struct qemu_pipe_dev *dev = pipe_dev;

	PIPE_D("Removing device\n");
	misc_deregister(&qemu_pipe_device);

	free_irq(dev->irq, pdev);

	iounmap(dev->base);
	if (dev->aps)
		kfree(dev->aps);
	dev->base = NULL;

	return 0;
}

static struct platform_driver qemu_pipe = {
	.probe = qemu_pipe_probe,
	.remove = qemu_pipe_remove,
	.driver = {
		.name = "qemu_pipe"
	}
};

static int __init qemu_pipe_dev_init(void)
{
	return platform_driver_register(&qemu_pipe);
}

static void qemu_pipe_dev_exit(void)
{
	platform_driver_unregister(&qemu_pipe);
}


module_init(qemu_pipe_dev_init);
module_exit(qemu_pipe_dev_exit);

MODULE_AUTHOR("David Turner <digit@google.com>");
MODULE_LICENSE("GPL");
