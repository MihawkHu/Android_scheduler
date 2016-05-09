/*
 * Copyright (C) 2014 Google, Inc.
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

#ifndef __MACH_GOLDFISH_PDEV_BUS_H
#define __MACH_GOLDFISH_PDEV_BUS_H

struct pdev_extra_resources {
	struct list_head list;
	const char *name;
	size_t num_resources;
	const struct resource *resource;
};

void goldfish_pdev_bus_add_extra_resources(struct pdev_extra_resources *extras);

#endif /* __MACH_GOLDFISH_PDEV_BUS_H */
