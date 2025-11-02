import threading
import queue
from typing import Any, Callable

try:
    from pythonosc.dispatcher import Dispatcher
    from pythonosc.osc_server import BlockingOSCUDPServer
except Exception:
    Dispatcher = None  # type: ignore
    BlockingOSCUDPServer = None  # type: ignore


class OscServer:
    """Threaded OSC server listening on 127.0.0.1:9002 and forwarding messages to a queue.

    Main thread should periodically drain `inbox` and update models/UI.
    """

    def __init__(self, host: str = "127.0.0.1", port: int = 9002) -> None:
        self.host = host
        self.port = port
        self.inbox: "queue.Queue[tuple[str, tuple[Any, ...]]]" = queue.Queue()
        self._thread: threading.Thread | None = None
        self._stop = threading.Event()

    def start(self) -> None:
        if Dispatcher is None or BlockingOSCUDPServer is None:
            print("[OSC_SERVER_STUB] python-osc not available; server disabled")
            return
        disp = Dispatcher()

        def _handler(address: str, *args: Any) -> None:
            try:
                print(f"[PY OSC SERVER] Message received: {address} with args {args}")
                self.inbox.put((address, args))
            except Exception:
                pass

        # Register address space
        disp.map("/info/audioDeviceList", _handler)
        disp.map("/info/currentSettings", _handler)
        disp.map("/info/masterGain", _handler)
        disp.map("/info/midiDeviceList", _handler)
        disp.map("/info/cpuLoad", _handler)

        server = BlockingOSCUDPServer((self.host, self.port), disp)

        def _run() -> None:
            while not self._stop.is_set():
                try:
                    server.handle_request()
                except Exception:
                    pass

        self._thread = threading.Thread(target=_run, name="OscServer9002", daemon=True)
        self._thread.start()

    def stop(self) -> None:
        self._stop.set()
        if self._thread and self._thread.is_alive():
            try:
                self._thread.join(timeout=0.5)
            except Exception:
                pass


