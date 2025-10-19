from typing import Any, Dict

try:
    from pythonosc.udp_client import SimpleUDPClient
except Exception:
    SimpleUDPClient = None  # type: ignore


class OscClient:
    """OSC client wrapper for JUCE engine on 127.0.0.1:9001."""

    def __init__(self, host: str = "127.0.0.1", port: int = 9001):
        self._host = host
        self._port = port
        self._client = SimpleUDPClient(host, port) if SimpleUDPClient else None

    def _send(self, addr: str, *args: Any) -> None:
        if self._client is not None:
            try:
                self._client.send_message(addr, args)
                return
            except Exception:
                pass
        # Fallback: log intent
        try:
            print(f"[OSC_STUB] {addr} {args}")
        except Exception:
            pass

    # API
    def create_voice(self, voice_id: int, voice_type: str, resource: str) -> None:
        self._send("/voice/create", int(voice_id), str(voice_type), str(resource))

    def destroy_voice(self, voice_id: int) -> None:
        self._send("/voice/destroy", int(voice_id))

    def update_parameter(self, voice_id: int, param: str, value: float) -> None:
        self._send(f"/voice/update/{param}", int(voice_id), float(value))

    # Listener/global
    def set_listener_pos(self, x: float, y: float) -> None:
        self._send("/listener/pos", float(x), float(y))

    def set_listener(self, radius_px: float, near_ratio: float) -> None:
        self._send("/listener/set", float(radius_px), float(near_ratio))

    def stop_all(self) -> None:
        self._send("/engine/stopAll")

    def create_voice_ex(self, voice_id: int, voice_type: str, resource: str, x: float, y: float, amplitude: float, pitch_on_grid: bool | None = None, looping: bool | None = None, volume: float | None = None) -> None:
        # canonical extended create with initial params and optional flags
        args: list[Any] = [int(voice_id), str(voice_type), str(resource), float(x), float(y), float(amplitude)]
        if pitch_on_grid is not None:
            args.append(1 if pitch_on_grid else 0)
        if looping is not None:
            args.append(1 if looping else 0)
        if volume is not None:
            args.append(float(volume))
        self._send("/voice/create_ex", *args)

    def update_voice_positions(self, positions: list[tuple[int, float, float]]) -> None:
        if not positions:
            return
        flat: list[Any] = []
        for vid, x, y in positions:
            flat.append(int(vid))
            flat.append(float(x))
            flat.append(float(y))
        self._send("/voices/update_positions", *flat)


_client = OscClient()


def get_ipc_client() -> OscClient:
    return _client


