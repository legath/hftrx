REM Utility files can be downloaded from https://sourceforge.net/projects/dfu-util/files/latest/download
del tc1_stm32mp157axx_app_old.stm32
dfu-util --alt 0 --dfuse-address 0x70080000 --upload "tc1_stm32mp157axx_app_old.stm32"
@pause


