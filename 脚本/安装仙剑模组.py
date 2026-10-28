"""分别安装、核验或卸载手柄、视角、金手指，保留存档。"""
import argparse
import hashlib
import json
import os
from pathlib import Path, PurePosixPath
import shutil
import subprocess
import sys
import zipfile

STATE_DIR = '.仙剑增强模组'
MODULES = ('手柄', '视角', '金手指')


def digest(path):
    with path.open('rb') as stream:
        return hashlib.file_digest(stream, 'sha256').hexdigest()


def safe_path(root, name):
    relative = PurePosixPath(name)
    if (not relative.parts or relative.is_absolute() or '\\' in name or ':' in name
            or '..' in relative.parts or relative.parts[0].lower() in ('save', 'screensave', STATE_DIR.lower())
            or relative.name.lower() in ('config.ini', 'setting.txt')):
        raise ValueError(f'模组不能处理此路径：{name}')
    path = root.joinpath(*relative.parts)
    if not path.resolve().is_relative_to(root.resolve()):
        raise ValueError(f'文件路径越出游戏目录：{name}')
    return path


def ensure_stopped():
    if os.name == 'nt':
        result = subprocess.run(['tasklist', '/FI', 'IMAGENAME eq PAL3.exe', '/FO', 'CSV', '/NH'],
                                capture_output=True, text=True, errors='replace', check=True)
        if 'pal3.exe' in result.stdout.lower():
            raise RuntimeError('请先关闭仙剑三，再安装或卸载模组')


def read_state(target):
    folder = target/STATE_DIR
    if folder.resolve() != target.resolve()/STATE_DIR:
        raise ValueError('安装记录目录不能指向游戏目录以外的位置')
    path = folder/'安装记录.json'
    state = json.loads(path.read_text('utf-8')) if path.exists() else {
        '格式': 2, '项目': '仙剑三增强', '目标': str(target.resolve()), '模组': {}, '文件': []}
    if state.get('格式') != 2:
        raise RuntimeError('请先使用旧版安装器卸载整合模组，再安装分功能模组')
    if state.get('项目') != '仙剑三增强' or state['目标'] != str(target.resolve()):
        raise RuntimeError('安装记录与当前目录不符')
    return folder, state


def save_state(folder, state):
    folder.mkdir(parents=True, exist_ok=True)
    temp = folder/'安装记录.json.swap'
    temp.write_text(json.dumps(state, ensure_ascii=False, indent=2), 'utf-8')
    os.replace(temp, folder/'安装记录.json')


def read_manifest(package):
    manifest = json.loads(package.read('模组清单.json'))
    if manifest['格式'] != 2 or manifest['项目'] != '仙剑三增强' or manifest['平台'] != 'PC':
        raise ValueError('请选择当前 PC 分功能模组包')
    paths = [row['路径'].lower() for row in manifest['文件']]
    if len(paths) != len(set(paths)):
        raise ValueError('模组包含重复目标文件')
    return manifest


def verify(target, rows):
    failed = []
    for row in rows:
        path = safe_path(target, row['路径'])
        if not path.is_file() or digest(path) != row['SHA256']:
            failed.append(row['路径'])
    if failed:
        raise RuntimeError('文件核验不符：'+'、'.join(failed[:12]))
    print(f'全部 {len(rows)} 个文件核验通过', flush=True)


def install(target, package, manifest):
    ensure_stopped()
    for row in manifest['原版识别']:
        path = safe_path(target, row['路径'])
        if not path.is_file() or digest(path) != row['SHA256']:
            raise RuntimeError(f'当前仅支持已核验的 Steam 版本：{row["路径"]}')
    folder, state = read_state(target)
    owned = {r['路径']: r for r in state['文件']}
    module = manifest['模组']
    module_id = module['标识']
    stage = folder/'暂存'
    pending = []
    rows = manifest['文件']
    try:
        for index, row in enumerate(rows, 1):
            dest = safe_path(target, row['路径'])
            current = digest(dest) if dest.is_file() else None
            previous = owned.get(row['路径'])
            if previous and current not in (previous['SHA256'], row['SHA256']):
                raise RuntimeError(f'文件被其他修改改变，未覆盖：{row["路径"]}')
            if previous and set(previous['所属模组'])-{module_id} and previous['SHA256'] != row['SHA256']:
                if not (row.get('共享') and previous.get('共享')):
                    raise RuntimeError(f'与其他模组的独立文件冲突：{row["路径"]}')
            if current == row['SHA256']:
                continue
            prepared = safe_path(stage, row['路径'])
            prepared.parent.mkdir(parents=True, exist_ok=True)
            data = package.read(row['载荷'])
            if hashlib.sha256(data).hexdigest() != row['载荷SHA256']:
                raise RuntimeError(f'压缩包载荷不符：{row["路径"]}')
            if row['类型'] == '差分':
                base = dest
                if not base.is_file() or digest(base) != row['原版SHA256']:
                    base = safe_path(folder/'原文件', row['路径'])
                if not base.is_file() or digest(base) != row['原版SHA256']:
                    raise RuntimeError(f'场景原文件版本不符：{row["路径"]}')
                shutil.copyfile(base, prepared)
                with prepared.open('r+b') as stream:
                    for offset, size, data_offset in row['差分块']:
                        if min(offset, size, data_offset) < 0 or offset+size > row['字节数'] or data_offset+size > len(data):
                            raise ValueError('场景差分范围不正确')
                        stream.seek(offset)
                        stream.write(data[data_offset:data_offset+size])
                    stream.truncate(row['字节数'])
            elif row['类型'] == '文件':
                prepared.write_bytes(data)
            else:
                raise ValueError('未知的模组文件类型')
            if digest(prepared) != row['SHA256'] or prepared.stat().st_size != row['字节数']:
                raise RuntimeError(f'准备输出不符：{row["路径"]}')
            pending.append((row, dest, prepared))
            if index % 200 == 0:
                print(f'正在准备 {module["名称"]}：{index}/{len(rows)}', flush=True)
        # 共用文件记录所有使用者；卸载一项不能删除其他项仍在使用的底座。
        for row in rows:
            record = owned.get(row['路径'])
            if not record:
                dest = safe_path(target, row['路径'])
                record = {'路径': row['路径'], '原来存在': dest.exists(),
                          '原来SHA256': digest(dest) if dest.is_file() else None,
                          '所属模组': [], '共享': bool(row.get('共享'))}
                state['文件'].append(record)
                owned[row['路径']] = record
            if record.get('SHA256') != row['SHA256']:
                record['上次SHA256'] = record.get('SHA256')
            record['SHA256'] = row['SHA256']
            if module_id not in record['所属模组']:
                record['所属模组'].append(module_id)
        state['模组'][module_id] = {'名称': module['名称'], '版本': manifest['版本']}
        save_state(folder, state)
        for row, dest, prepared in pending:
            backup = safe_path(folder/'原文件', row['路径'])
            if owned[row['路径']]['原来存在'] and not backup.exists():
                backup.parent.mkdir(parents=True, exist_ok=True)
                os.replace(dest, backup)
            dest.parent.mkdir(parents=True, exist_ok=True)
            os.replace(prepared, dest)
        verify(target, rows)
        print(f'已安装 {module["名称"]} {manifest["版本"]}，存档保持原样', flush=True)
    finally:
        if stage.exists():
            shutil.rmtree(stage)


def uninstall(target, names=None):
    ensure_stopped()
    folder, state = read_state(target)
    chosen = {key for key, item in state['模组'].items() if not names or item['名称'] in names}
    if not chosen:
        raise RuntimeError('没有找到所选的已安装模组')
    removing = [r for r in state['文件'] if set(r['所属模组']) & chosen and not set(r['所属模组'])-chosen]
    for row in removing:
        dest = safe_path(target, row['路径'])
        current = digest(dest) if dest.is_file() else None
        if current not in (row['SHA256'], row.get('上次SHA256'), row['原来SHA256'], None):
            raise RuntimeError(f'文件被其他修改改变，停止卸载：{row["路径"]}')
        backup = safe_path(folder/'原文件', row['路径'])
        if row['原来存在'] and (not backup.is_file() or digest(backup) != row['原来SHA256']):
            if current != row['原来SHA256']:
                raise RuntimeError(f'恢复文件缺失或不符：{row["路径"]}')
    for row in reversed(removing):
        dest = safe_path(target, row['路径'])
        backup = safe_path(folder/'原文件', row['路径'])
        if row['原来存在'] and backup.exists():
            os.replace(backup, dest)
        elif not row['原来存在'] and dest.exists():
            dest.unlink()
    for row in state['文件']:
        row['所属模组'] = [name for name in row['所属模组'] if name not in chosen]
    state['文件'] = [row for row in state['文件'] if row['所属模组']]
    labels = [state['模组'].pop(key)['名称'] for key in sorted(chosen)]
    if state['模组']:
        save_state(folder, state)
    else:
        shutil.rmtree(folder)
    for row in removing:
        parent = safe_path(target, row['路径']).parent
        while parent != target:
            try: parent.rmdir()
            except OSError: break
            parent = parent.parent
    print('已卸载 '+'、'.join(labels)+'；其他模组、存档和个人设置保持原样', flush=True)


class DirectoryPackage:
    """读取解压后的发行目录，与 ZIP 安装使用同一份文件清单。"""
    def __init__(self, root):
        self.root = root.resolve()

    def read(self, name):
        return safe_path(self.root, name).read_bytes()

    def __enter__(self):
        return self

    def __exit__(self, *_):
        return False


def open_package(path):
    return DirectoryPackage(path) if path.is_dir() else zipfile.ZipFile(path)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('操作', choices=('安装', '核验', '卸载'))
    parser.add_argument('--目标', type=Path)
    parser.add_argument('--包', type=Path, nargs='+')
    parser.add_argument('--模组', choices=MODULES, nargs='+')
    args = parser.parse_args()
    target = (args.目标 or Path(input('请输入包含 PAL3.exe 的目录：').strip().strip('"'))).resolve()
    if not (target/'PAL3.exe').is_file():
        raise RuntimeError('所选目录没有 PAL3.exe')
    if args.操作 == '卸载':
        uninstall(target, args.模组)
    elif args.操作 == '核验':
        _, state = read_state(target)
        ids = {key for key, item in state['模组'].items() if not args.模组 or item['名称'] in args.模组}
        if not ids: raise RuntimeError('没有找到所选的已安装模组')
        verify(target, [r for r in state['文件'] if set(r['所属模组']) & ids])
    else:
        files = args.包 or ([Path(__file__).parent] if (Path(__file__).parent/'模组清单.json').is_file() else sorted(Path(__file__).parent.glob('仙剑三-PC-*.zip')))
        count = 0
        for path in files:
            with open_package(path) as package:
                manifest = read_manifest(package)
                if args.模组 and manifest['模组']['名称'] not in args.模组: continue
                install(target, package, manifest)
                count += 1
        if not count: raise RuntimeError('没有找到对应 PC 模组包，请将安装器与 ZIP 放在同一目录')


if __name__ == '__main__':
    try: main()
    except (OSError, ValueError, RuntimeError, KeyError, zipfile.BadZipFile) as error:
        print(f'停止：{error}', file=sys.stderr)
        sys.exit(1)
