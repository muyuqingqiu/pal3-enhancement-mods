"""构建不含高清功能的 PC 插件，制作可分别安装的三个发行包。"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import shutil
import subprocess
import sys
import zipfile

ROOT = Path(__file__).resolve().parents[1]
SOURCE = ROOT/'源码'
BUILD = ROOT/'构建产物'
MODULES = {'手柄': ('controller', 'Controller'), '视角': ('camera', 'Camera'), '金手指': ('cheats', 'Cheats')}


def digest(path):
    with path.open('rb') as stream:
        return hashlib.file_digest(stream, 'sha256').hexdigest()


def run(command, env):
    subprocess.run([str(x) for x in command], cwd=ROOT, env=env, check=True)


def build(zig, sdk, version, checks):
    BUILD.mkdir(exist_ok=True)
    temporary = BUILD/'临时文件'
    temporary.mkdir(exist_ok=True)
    env = dict(os.environ, TEMP=str(temporary), TMP=str(temporary),
               ZIG_GLOBAL_CACHE_DIR=str(BUILD/'全局缓存'), ZIG_LOCAL_CACHE_DIR=str(BUILD/'项目缓存'),
               PYTHONDONTWRITEBYTECODE='1', PYTHONIOENCODING='utf-8')
    if not (sdk/'PAL3patch.h').is_file():
        raise RuntimeError('SDK 目录必须包含 PAL3patch.h')
    zig_version = subprocess.check_output([str(zig), 'version'], env=env, text=True).strip()
    if zig_version != '0.17.0':
        raise RuntimeError('当前正式构建使用 Zig 0.17.0，请使用该版本')
    header = BUILD/'版本.h'
    header.write_text(f'#define PLUGIN_VERSION "{version}"\n', 'utf-8')
    library = BUILD/'PAL3patch-import.lib'
    run([zig, 'dlltool', '-m', 'i386', '-D', 'PAL3patch.dll', '-d', SOURCE/'PAL3patch导入.def', '-l', library], env)
    sources = [SOURCE/name for name in ('手柄插件.c', '手柄菜单导航.c', '手柄场景输入.c',
                                        '手柄战斗导航.c', '镜头遮挡.c', '模组开关.c')]
    common = [zig, 'cc', '-target', 'x86-windows-gnu', '-O2', '-fno-stack-protector', '-I'+str(sdk)]
    output = BUILD/'PAL3Gamepad.dll'
    run(common+['-shared', '-Wno-error=date-time', '-include', header, '-DPAL3_SCENE_INPUT_NATIVE=1',
                *sources, library, '-o', output, '-luser32', '-lkernel32'], env)
    print(f'插件编译完成：{output.stat().st_size:,} 字节', flush=True)
    if checks:
        for name, dependencies in (
            ('手柄插件定向验证', ['模组开关.c']),
            ('模组组合定向验证', ['模组开关.c']),
            ('手柄场景输入定向验证', ['手柄场景输入.c'])):
            exe = BUILD/(name+'.exe')
            run(common+['-ffunction-sections', '-fdata-sections', '-Wl,--gc-sections', SOURCE/(name+'.c'),
                        *[SOURCE/n for n in dependencies], library, '-o', exe, '-luser32', '-lkernel32'], env)
            run([exe], env)
        run([sys.executable, '-B', ROOT/'脚本/安装器定向验证.py'], env)
    info = {'版本': version, '平台': 'PC-x86', 'Zig': zig_version,
            '插件': {'路径': output.name, '字节数': output.stat().st_size, 'SHA256': digest(output)},
            'SDK主头文件SHA256': digest(sdk/'PAL3patch.h'), '诊断入口': False,
            '源码': {p.name: digest(p) for p in sources}, '定向检查': checks}
    (BUILD/'构建信息.json').write_text(json.dumps(info, ensure_ascii=False, indent=2), 'utf-8')
    return info


def package(label, version, anchors):
    module, english = MODULES[label]
    output = ROOT/'发行产物'/version
    output.mkdir(parents=True, exist_ok=True)
    filename = f'PAL3-{english}-{version}.zip'
    destination = output/filename
    temporary = destination.with_suffix('.zip.swap')
    manifest = {'格式': 2, '项目': '仙剑三增强', '平台': 'PC', '版本': version,
                '模组': {'标识': module, '名称': label}, '原版识别': anchors, '文件': []}
    files = [('plugins/PAL3Gamepad.dll', (BUILD/'PAL3Gamepad.dll').read_bytes(), True),
             ('mods/pal3/modular.enabled', b'1\n', True), (f'mods/pal3/{module}.enabled', b'1\n', False)]
    with zipfile.ZipFile(temporary, 'w', compression=zipfile.ZIP_DEFLATED, compresslevel=9) as archive:
        for index, (name, data, shared) in enumerate(files):
            payload = f'文件/{index:04d}.bin'
            checksum = hashlib.sha256(data).hexdigest()
            manifest['文件'].append({'路径': name, '类型': '文件', '字节数': len(data), 'SHA256': checksum,
                '载荷': payload, '载荷SHA256': checksum, '共享': shared})
            archive.writestr(payload, data)
        archive.writestr('模组清单.json', json.dumps(manifest, ensure_ascii=False, indent=2))
        archive.write(ROOT/'脚本/安装仙剑模组.py', '安装仙剑模组.py')
        archive.write(ROOT/'文档/安装与操作.md', '模组说明.md')
        archive.write(ROOT/'文档/第三方说明.md', '第三方说明.md')
        archive.write(ROOT/'LICENSE', '许可证.txt')
        for action in ('安装', '核验', '卸载'):
            text = ('@echo off\r\nchcp 65001 >nul\r\n'
                    f'py -3 -B "%~dp0安装仙剑模组.py" {action} --模组 {label} %*\r\n'
                    'if errorlevel 1 echo 操作未完成，请查看上方提示。\r\npause\r\n')
            archive.writestr(action+'.cmd', text.encode('utf-8'))
        if module in ('controller', 'camera'):
            archive.write(ROOT/'脚本/生成地图查询数据.py', '生成地图查询数据.py')
            archive.write(ROOT/'地图数据依赖.txt', '地图数据依赖.txt')
            text = ('@echo off\r\nchcp 65001 >nul\r\n'
                    'set /p "PAL3_GAME_DIR=请输入包含 PAL3.exe 的游戏目录："\r\n'
                    'py -3 -B "%~dp0生成地图查询数据.py" --游戏目录 "%PAL3_GAME_DIR%"\r\n'
                    'if errorlevel 1 echo 请先按模组说明安装地图数据依赖，再重新运行。\r\npause\r\n')
            archive.writestr('生成地图数据.cmd', text.encode('utf-8'))
    with zipfile.ZipFile(temporary) as archive:
        for row in manifest['文件']:
            if hashlib.sha256(archive.read(row['载荷'])).hexdigest() != row['SHA256']:
                raise RuntimeError('包内插件核验失败')
    os.replace(temporary, destination)
    report = {'文件': filename, '模组': label, '版本': version, '字节数': destination.stat().st_size,
              'SHA256': digest(destination), '插件SHA256': manifest['文件'][0]['SHA256'],
              '平台': 'PC', '高清素材': False, '游戏资源': False}
    print('完成 '+filename+f'：{destination.stat().st_size:,} 字节', flush=True)
    return report


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--zig', type=Path, required=True)
    parser.add_argument('--sdk', type=Path, required=True)
    parser.add_argument('--版本', default='0.3.0')
    parser.add_argument('--模组', choices=tuple(MODULES), nargs='+', default=list(MODULES))
    parser.add_argument('--检查', action='store_true')
    args = parser.parse_args()
    if not re.fullmatch(r'\d+\.\d+\.\d+', args.版本):
        parser.error('版本号须为三段数字')
    info = build(args.zig.resolve(), args.sdk.resolve(), args.版本, args.检查)
    anchors = json.loads((ROOT/'兼容版本.json').read_text('utf-8'))['原版识别']
    reports = [package(label, args.版本, anchors) for label in dict.fromkeys(args.模组)]
    output = ROOT/'发行产物'/args.版本
    (output/'BUILD-MANIFEST.json').write_text(json.dumps({'构建': info, '附件': reports}, ensure_ascii=False, indent=2), 'utf-8')
    (output/'SHA256.txt').write_text(''.join(r['SHA256']+'  '+r['文件']+'\n' for r in reports), 'utf-8')
    print('所选 PC 模组构建与打包完成', flush=True)


if __name__ == '__main__':
    main()
