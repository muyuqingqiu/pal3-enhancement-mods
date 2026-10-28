"""验证共享文件、独立卸载、升级恢复和存档保护，不使用真实游戏数据。"""
import hashlib
import importlib.util
import io
import itertools
import json
from pathlib import Path
import tempfile
import unittest
import zipfile

HERE = Path(__file__).resolve().parent
spec = importlib.util.spec_from_file_location('pal3_installer', HERE/'安装仙剑模组.py')
installer = importlib.util.module_from_spec(spec)
spec.loader.exec_module(installer)
installer.ensure_stopped = lambda: None


def sha(data):
    return hashlib.sha256(data).hexdigest()


class InstallTests(unittest.TestCase):
    def setUp(self):
        parent = HERE.parent/'构建产物/安装器定向验证'
        parent.mkdir(parents=True, exist_ok=True)
        self.folder = tempfile.TemporaryDirectory(dir=parent)
        self.root = Path(self.folder.name).resolve()
        (self.root/'PAL3.exe').write_bytes(b'original-exe')
        (self.root/'common.dll').write_bytes(b'original-dll')
        (self.root/'save').mkdir()
        (self.root/'save/1.sav').write_bytes(b'untouched-save')
        self.labels = {'controller': '手柄', 'camera': '视角', 'cheats': '金手指'}

    def tearDown(self):
        self.assertEqual((self.root/'save/1.sav').read_bytes(), b'untouched-save')
        self.folder.cleanup()

    def package(self, module, shared=b'shared-v1', extra=None):
        entries = [('common.dll', shared, True), (f'mods/pal3/{module}.enabled', b'1\n', False)]
        if extra:
            entries += extra
        manifest = {'格式': 2, '项目': '仙剑三增强', '平台': 'PC', '版本': '0.3.0',
                    '模组': {'标识': module, '名称': self.labels[module]},
                    '原版识别': [{'路径': 'PAL3.exe', 'SHA256': sha(b'original-exe')}], '文件': []}
        buffer = io.BytesIO()
        with zipfile.ZipFile(buffer, 'w') as archive:
            for index, (name, data, shared_file) in enumerate(entries):
                payload = f'文件/{index}.bin'
                archive.writestr(payload, data)
                manifest['文件'].append({'路径': name, '类型': '文件', '字节数': len(data),
                    'SHA256': sha(data), '载荷': payload, '载荷SHA256': sha(data), '共享': shared_file})
            archive.writestr('模组清单.json', json.dumps(manifest))
        buffer.seek(0)
        return zipfile.ZipFile(buffer), manifest

    def install(self, module, **kwargs):
        archive, manifest = self.package(module, **kwargs)
        with archive:
            installer.install(self.root, archive, installer.read_manifest(archive))

    def test_all_combinations_and_separate_uninstall(self):
        for length in range(1, 4):
            for chosen in itertools.combinations(self.labels, length):
                with self.subTest(chosen=chosen):
                    for module in chosen:
                        self.install(module)
                    for index, module in enumerate(chosen):
                        installer.uninstall(self.root, [self.labels[module]])
                        remaining = chosen[index+1:]
                        self.assertEqual((self.root/'common.dll').read_bytes(),
                                         b'shared-v1' if remaining else b'original-dll')
                        for name in remaining:
                            self.assertTrue((self.root/f'mods/pal3/{name}.enabled').is_file())
                    self.assertFalse((self.root/installer.STATE_DIR).exists())

    def test_shared_upgrade_keeps_other_owner_and_original(self):
        self.install('controller')
        self.install('camera')
        self.install('camera', shared=b'shared-v2')
        _, state = installer.read_state(self.root)
        installer.verify(self.root, state['文件'])
        installer.uninstall(self.root, ['视角'])
        self.assertEqual((self.root/'common.dll').read_bytes(), b'shared-v2')
        installer.uninstall(self.root)
        self.assertEqual((self.root/'common.dll').read_bytes(), b'original-dll')

    def test_extracted_release_installs_without_original_zip(self):
        archive, _ = self.package('controller')
        folder = self.root/'下载包'
        folder.mkdir()
        with archive:
            for name in archive.namelist():
                path = installer.safe_path(folder, name)
                path.parent.mkdir(parents=True, exist_ok=True)
                path.write_bytes(archive.read(name))
        with installer.open_package(folder) as package:
            installer.install(self.root, package, installer.read_manifest(package))
        self.assertEqual((self.root/'common.dll').read_bytes(), b'shared-v1')
        installer.uninstall(self.root)
        self.assertEqual((self.root/'common.dll').read_bytes(), b'original-dll')

    def test_bad_payload_stops_before_changing_game(self):
        archive, manifest = self.package('controller')
        manifest['文件'][0]['载荷SHA256'] = '0'*64
        with archive, self.assertRaisesRegex(RuntimeError, '载荷不符'):
            installer.install(self.root, archive, manifest)
        self.assertEqual((self.root/'common.dll').read_bytes(), b'original-dll')
        self.assertFalse((self.root/'mods/pal3/controller.enabled').exists())

    def test_unknown_edit_blocks_upgrade_and_uninstall(self):
        self.install('controller')
        (self.root/'common.dll').write_bytes(b'user-edited')
        with self.assertRaisesRegex(RuntimeError, '其他修改'):
            self.install('controller', shared=b'shared-v2')
        with self.assertRaisesRegex(RuntimeError, '其他修改'):
            installer.uninstall(self.root)
        self.assertEqual((self.root/'common.dll').read_bytes(), b'user-edited')

    def test_preexisting_identical_file_remains_after_uninstall(self):
        (self.root/'common.dll').write_bytes(b'shared-v1')
        self.install('controller')
        installer.uninstall(self.root)
        self.assertEqual((self.root/'common.dll').read_bytes(), b'shared-v1')

    def test_saved_paths_and_traversal_are_rejected(self):
        for name in ('save/1.sav', 'screensave/x.bmp', 'config.ini', 'setting.txt',
                     '../outside', '/absolute', 'C:/target', 'save\\1.sav'):
            with self.subTest(name=name), self.assertRaises(ValueError):
                installer.safe_path(self.root, name)

    def test_delta_upgrade_uses_original_backup(self):
        original = b'original-scene'
        (self.root/'scene').mkdir()
        (self.root/'scene/test.cpk').write_bytes(original)
        for data in (b'first-new-scene', b'second-new-scene'):
            archive, manifest = self.package('controller', extra=[('scene/test.cpk', data, False)])
            row = manifest['文件'][-1]
            row.update(类型='差分', 原版SHA256=sha(original), 差分块=[[0, len(data), 0]])
            with archive:
                installer.install(self.root, archive, manifest)
            self.assertEqual((self.root/'scene/test.cpk').read_bytes(), data)
        installer.uninstall(self.root)
        self.assertEqual((self.root/'scene/test.cpk').read_bytes(), original)


if __name__ == '__main__':
    unittest.main()
