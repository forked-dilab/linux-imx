# File: linux-imx/security/topmon/Makefile
# Overview: Link TOPMON monitoring and optional TEE notification into one kernel object.

obj-$(CONFIG_TOPMON_CORE) += topmon-monitor.o

topmon-monitor-y := topmon-lkrg-integrity.o common/topmon-hash.o

topmon-monitor-$(CONFIG_TOPMON_LKRG) += topmon-lkrg-integrity-check.o
topmon-monitor-$(CONFIG_TOPMON_LKRG) += topmon-lkrg-integrity-notifier.o
topmon-monitor-$(CONFIG_TOPMON_LKRG_TEE) += topmon-lkrg-integrity-tee.o

# End of File: linux-imx/security/topmon/Makefile
