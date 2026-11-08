"""
Auth deploy: 复制新增源文件到 Chromium 对应位置
"""
import shutil
from pathlib import Path


def run(chromium_src: Path, kernel_root: Path, project_root: Path) -> None:
    chromium_src = Path(chromium_src)
    project_root = Path(project_root)
    sources_dir = project_root / "sources"

    if not sources_dir.is_dir():
        print("No sources directory found, skipping deploy.")
        return

    # 复制 mz/auth
    src_auth = sources_dir / "mz" / "auth"
    dst_auth = chromium_src / "mz" / "auth"
    if src_auth.is_dir():
        dst_auth.mkdir(parents=True, exist_ok=True)
        for f in src_auth.iterdir():
            if f.is_file():
                shutil.copy2(f, dst_auth / f.name)
                print(f"  {f.name} -> mz/auth/")

    print("Done. Auth sources deployed.")
