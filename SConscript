from building import *

cwd = GetCurrentDir()
src = []
path = [cwd]
CPPDEFINES = []

if GetDepend('PKG_USING_WLAN_CYW43439'):
    src += [
        cwd + '/drv_wifi_cyw43439.c',
        cwd + '/pico-source/src/async_context_rtthread.c',
        cwd + '/pico-source/src/cyw43_arch.c',
        cwd + '/pico-source/src/cyw43_arch_rtthread.c',
        cwd + '/pico-source/src/lwip_rtthread.c',
    ]
    path += [
        cwd + '/pico-source/inc',
    ]

    CPPDEFINES = [
        'PICO_CYW43_ARCH_RTTHREAD',
        'PICO_CYW43_SUPPORTED',
        'PICO_BOARD=pico_w',
        'CYW43_LWIP=1',
        'PICO_CONFIG_HEADER=boards/pico_w.h',
    ]

group = DefineGroup('cyw43439', src, depend = [''], CPPPATH = path,  CPPDEFINES = CPPDEFINES)

Return('group')
