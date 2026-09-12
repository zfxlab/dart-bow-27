"""Reproducible H7/F4 configuration and build matrix; never edits board profiles."""
import argparse
import json
import re
from pathlib import Path
import shutil
import subprocess

ROOT = Path(__file__).resolve().parents[2]

def run(args, log, expected=None):
    result = subprocess.run([str(x) for x in args], cwd=ROOT, stdout=subprocess.PIPE,
                            stderr=subprocess.STDOUT, text=True, encoding='utf-8', errors='replace')
    log.write_text(result.stdout, encoding='utf-8')
    if expected:
        assert result.returncode and expected in result.stdout, result.stdout
    elif result.returncode:
        raise RuntimeError(f'{log}\n{result.stdout[-6000:]}')
    return result.stdout

def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--board', choices=['all','h723_mc02','f407_c_board'], default='all')
    parser.add_argument('--configure-only', action='store_true')
    parser.add_argument('--build-type', choices=['Debug','Release'], default='Debug')
    parser.add_argument('--h7-bsp', type=Path, default=ROOT/'pnx_bsp')
    parser.add_argument('--f4-bsp', type=Path, default=ROOT/'pnx_bsp')
    args=parser.parse_args()
    output=ROOT/'build/v2-validation'/args.build_type
    output.mkdir(parents=True,exist_ok=True)
    results=[]
    for board in (['h723_mc02','f407_c_board'] if args.board=='all' else [args.board]):
        h7=board=='h723_mc02'; uart='uart7' if h7 else 'usart1'; can='fdcan1' if h7 else 'can2'
        cases=[
            ('minimal',{},{}),
            ('uart',{'test':{'auto_run_on_boot':True,'usart':True},'bindings':{'uart_ports':{'test_uart':uart}}},{}),
            ('can',{'test':{'auto_run_on_boot':True,'can':True},'bindings':{'can_buses':{'test_can':can}}},{}),
            ('usb_on',{'build':{'usbx':True},'test':{'auto_run_on_boot':True,'usb':True}},{}),
            ('usb_off',{'build':{'usbx':False},'usb':{'period_ticks':'unused'}},{}),
            ('bmi_off',{'ahrs':{'solver':'unused'},'bmi088':{'pid_kp':'unused'},'dmimu':{'communication_mode':'unused'},'ps2':{'backend':'unused'},'remoter':{'thread_priority':'unused'},'can_diag':{'window_size':'unused'}},{}),
            ('quaternion',{'ahrs':{'solver':'quaternion_ekf','imu_offset_x':0.1},'test':{'auto_run_on_boot':True,'imu':True}},{'devices':{'bmi088':{'enabled':True}}}),
            ('tactical',{'ahrs':{'solver':'tactical_ekf'},'test':{'auto_run_on_boot':True,'imu':True}},{'devices':{'bmi088':{'enabled':True}}}),
        ]
        build=output/board/'firmware'
        for name,params,robot in cases:
            dest=output/board/name;dest.mkdir(parents=True,exist_ok=True)
            paramfile=dest/'params.json';robotfile=dest/'robot.json'
            paramfile.write_text(json.dumps(params,indent=2));robotfile.write_text(json.dumps(robot,indent=2))
            run(['cmake','-S',ROOT,'-B',build,'-G','Ninja',f'-DPNX_BOARD={board}',
                 f'-DCMAKE_BUILD_TYPE={args.build_type}',f'-DPNX_BSP_SOURCE_DIR={(args.h7_bsp if h7 else args.f4_bsp).resolve()}',
                 f'-DPNX_PARAMS_OVERRIDE={paramfile}',f'-DPNX_ROBOT_CONFIG_OVERRIDE={robotfile}'],dest/'configure.log')
            header=(build/'generated/config.hpp').read_text()
            if name=='minimal':
                assert '#define HAS_AHRS 0' in header and '#define ENABLE_USBX 0' in header
                assert ' test_can =' not in header and ' test_uart =' not in header
            if name in ['quaternion','tactical']:
                assert '#define HAS_AHRS 1' in header
                assert f'use_tactical = {str(name=="tactical").lower()}' in header
                assert 'target_temp = 45' in header and 'temp_thread_priority = 4' in header
                assert abs(float(re.search(r'float boost_duty = ([0-9.eE+-]+)f',header)[1])-(0.06 if h7 else 0.9))<1e-6
                assert f'loop_sleep_ticks = {0 if h7 else 1}' in header
            if not args.configure_only:
                run(['cmake','--build',build,'--parallel','8'],dest/'build.log')
                shutil.copy2(build/'pnx_embedded.elf',dest/'pnx_embedded.elf')
                symbols=run(['arm-none-eabi-nm','--defined-only',build/'pnx_embedded.elf'],dest/'symbols.log')
                for entry in ['App_ThreadX_Init','MX_ThreadX_Init','app_start']:
                    assert sum(line.endswith(' '+entry) for line in symbols.splitlines())==1,entry
            results.append({'board':board,'case':name,'built':not args.configure_only,'passed':True})
            (output/'results.json').write_text(json.dumps(results,indent=2))
            print(f'PASS {board}/{name}',flush=True)
        for test,required in [('usart','test_uart'),('can','test_can'),('usb','build.usbx'),('imu','robot.devices.bmi088')]:
            dest=output/board/('missing-'+test);dest.mkdir(exist_ok=True)
            param=dest/'params.json';robot=dest/'robot.json';param.write_text(json.dumps({'test':{test:True}}));robot.write_text('{}')
            run(['cmake',f'-DIOC={ROOT}/boards/{board}/{board}.ioc',f'-DBOARD_CONFIG={ROOT}/boards/{board}/board.json',
                 f'-DPARAMS={param}',f'-DROBOT_CONFIG={robot}',f'-DOUT_DIR={dest}/generated','-DPNX_BOARD_FAMILY='+('stm32h7' if h7 else 'stm32f4'),
                 '-P',ROOT/'configs/cmake/generate_config.cmake'],dest/'configure.log',expected=required)
            results.append({'board':board,'case':'missing-'+test,'expected_failure':True,'passed':True})
            (output/'results.json').write_text(json.dumps(results,indent=2))
        print(f'PASS {board}/missing-resource checks',flush=True)
    print(f'Results: {output}/results.json')

if __name__=='__main__':
    main()
