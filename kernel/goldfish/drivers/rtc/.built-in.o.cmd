cmd_drivers/rtc/built-in.o :=  arm-linux-androideabi-ld -EL    -r -o drivers/rtc/built-in.o drivers/rtc/rtc-lib.o drivers/rtc/hctosys.o drivers/rtc/rtc-core.o drivers/rtc/rtc-goldfish.o 
