Import("env")
import os, shutil

os.chdir('html')
target_name = env['PIOENV'].upper()

type = 'tx' if '_TX_' in target_name else 'rx'
is8285 = '-8285' if 'ESP8285' in env['PIOENV'] else ''
chip = 'sx128x' if '-DRADIO_SX128X=1' in env['BUILD_FLAGS'] else 'sx127x' if '-DRADIO_SX127X=1' in env['BUILD_FLAGS'] else 'lr1121' if '-DRADIO_LR1121=1' in env['BUILD_FLAGS'] else ''

try:
    shutil.copy(f'headers/web-{chip}-{type}{is8285}.h', '../include/WebContent.h')
except FileNotFoundError as ex:
    if not os.path.isfile('../include/WebContent.h'):
        raise ex
    print("WARNING: html header files not found in `html/headers` but `../include/WebContent.h` exists, using old `WebContent.h` instead")
