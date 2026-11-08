"""
部署 review 模块的资源文件到 Chromium 源码树
"""
import shutil
from pathlib import Path


def run(chromium_src: Path, kernel_root: Path, project_root: Path) -> None:
    """
    部署资源文件
    - 复制 chrome/browser/mz 源文件
    - 复制 chrome/browser/resources/review 前端资源
    - 复制 chrome/browser/ui/webui/review 后端代码
    """
    sources_dir = project_root / "sources"
    if not sources_dir.is_dir():
        return

    # 部署 chrome/browser/mz
    mz_src = sources_dir / "chrome_browser_mz"
    if mz_src.is_dir():
        mz_dst = chromium_src / "chrome" / "browser" / "mz"
        mz_dst.mkdir(parents=True, exist_ok=True)
        for src_file in mz_src.glob("*"):
            if src_file.is_file():
                dst_file = mz_dst / src_file.name
                print(f"Deploying: chrome/browser/mz/{src_file.name}")
                shutil.copy2(src_file, dst_file)

    # 部署 chrome/browser/resources/review
    resources_src = sources_dir / "chrome_browser_resources_review"
    if resources_src.is_dir():
        resources_dst = chromium_src / "chrome" / "browser" / "resources" / "review"
        resources_dst.mkdir(parents=True, exist_ok=True)
        for src_file in resources_src.glob("*"):
            if src_file.is_file():
                dst_file = resources_dst / src_file.name
                print(f"Deploying: chrome/browser/resources/review/{src_file.name}")
                shutil.copy2(src_file, dst_file)

    # 部署 chrome/browser/ui/webui/review
    webui_src = sources_dir / "chrome_browser_ui_webui_review"
    if webui_src.is_dir():
        webui_dst = chromium_src / "chrome" / "browser" / "ui" / "webui" / "review"
        webui_dst.mkdir(parents=True, exist_ok=True)
        for src_file in webui_src.glob("*"):
            if src_file.is_file():
                dst_file = webui_dst / src_file.name
                print(f"Deploying: chrome/browser/ui/webui/review/{src_file.name}")
                shutil.copy2(src_file, dst_file)
