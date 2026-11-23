"""Run SSH release checks against an isolated loopback server and temporary home."""
import argparse
import os
from pathlib import Path
import re
import socket
import subprocess
import sys
import tempfile
import threading
import time


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--engine", type=Path, required=True)
    parser.add_argument("--ui", type=Path)
    args = parser.parse_args()
    engine = args.engine.resolve(strict=True)
    ui = args.ui.resolve(strict=True) if args.ui else None
    server_script = Path(__file__).with_name("dev_ssh_server.py").resolve()
    with tempfile.TemporaryDirectory(prefix="aino-ssh-release-") as directory:
        root = Path(directory)
        (root / "build").mkdir()
        env = dict(os.environ, PYTHONUTF8="1", AINO_SSH_HOME=str(root / "home"))
        (root / "home").mkdir()
        log = root / "server.log"
        with log.open("w", encoding="utf-8") as output:
            server = subprocess.Popen(
                [sys.executable, "-u", str(server_script), "--port", "0", "--root", str(root / "remote")],
                cwd=root, env=env, stdout=output, stderr=subprocess.STDOUT,
                creationflags=subprocess.CREATE_NO_WINDOW if os.name == "nt" else 0)
            try:
                deadline = time.monotonic() + 20
                port = None
                while time.monotonic() < deadline:
                    match = re.search(r"127\.0\.0\.1:(\d+)", log.read_text(encoding="utf-8"))
                    if match:
                        port = match[1]
                        break
                    if server.poll() is not None:
                        raise RuntimeError("SSH test server exited before readiness")
                    time.sleep(0.05)
                if port is None:
                    raise TimeoutError("SSH test server did not become ready")
                env["SSH_PROBE_PORT"] = port
                env["SSH_PROBE_HIGH_FD"] = "1"
                # 接受 TCP 但不发送 SSH banner，验证取消能结束握手等待。
                with socket.socket() as silent:
                    silent.bind(("127.0.0.1", 0))
                    silent.listen(1)
                    silent.settimeout(5)
                    def hold_banner():
                        try:
                            peer, _ = silent.accept()
                            with peer:
                                peer.settimeout(5)
                                while peer.recv(1024):
                                    pass
                        except (OSError, TimeoutError):
                            pass
                    stalled = threading.Thread(target=hold_banner)
                    stalled.start()
                    try:
                        subprocess.run([str(engine), "--cancel-handshake", str(silent.getsockname()[1])],
                            cwd=root, env=env, check=True, timeout=30)
                    finally:
                        stalled.join(timeout=6)
                        if stalled.is_alive():
                            raise TimeoutError("silent test peer did not exit")
                subprocess.run([str(engine)], cwd=root, env=env, check=True, timeout=90)
                if ui:
                    subprocess.run([str(ui), "--local-transfer", port], cwd=root, env=env, check=True, timeout=90)
                print("SSH release check: PASS (isolated server, home and local files)")
            except Exception:
                print(log.read_text(encoding="utf-8", errors="replace"), file=sys.stderr)
                raise
            finally:
                server.terminate()
                try:
                    server.wait(timeout=5)
                except subprocess.TimeoutExpired:
                    server.kill()
                    server.wait(timeout=5)


if __name__ == "__main__":
    main()
