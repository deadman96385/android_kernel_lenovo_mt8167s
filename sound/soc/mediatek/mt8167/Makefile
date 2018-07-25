MTK_PLATFORM := $(subst ",,$(CONFIG_MTK_PLATFORM))
subdir-ccflags-y += -Werror -I$(srctree)/drivers/misc/mediatek/base/power/$(MTK_PLATFORM)

snd-soc-mt8167-pcm-objs := \
    mt8167-afe-pcm.o mt8167-afe-util.o mt8167-afe-controls.o mt8167-afe-debug.o

snd-soc-mt8167-mt6392-mach-objs := \
    mt8167_evb.o

snd-soc-mt8516-polk-at1-mach-objs := \
    polk-at1.o

snd-soc-mt8516-mt7668-ref-mach-objs := \
    mt8516-mt7668-ref.o

obj-$(CONFIG_SND_SOC_MT8167) += snd-soc-mt8167-pcm.o
obj-$(CONFIG_SND_SOC_MT8167_MT6392_MACH) += snd-soc-mt8167-mt6392-mach.o
obj-$(CONFIG_SND_SOC_MT8516_P1_MACH) += mt8516-p1.o
obj-$(CONFIG_SND_SOC_MT8516_POLK_AT1_MACH) += snd-soc-mt8516-polk-at1-mach.o
obj-$(CONFIG_SND_SOC_MT8167S_SOM_MACH) += mt8167s-som.o
obj-$(CONFIG_SND_SOC_MT8516_MT7668_REF_MACH) += snd-soc-mt8516-mt7668-ref-mach.o
