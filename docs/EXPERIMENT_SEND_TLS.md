# AirDrop outbound TLS experiment (read before testing)

Goal: determine precisely why a Windows sender with a self-signed certificate cannot complete a TLS connection to an iPhone with AirDrop set to Everyone for 10 Minutes. This does **not** claim self-signed certificates are accepted on current iOS.

## Known baseline
- The existing `tools/airdrop_send.py` uses WinAWDL's MIT-licensed sender and the local outbound proxy at `127.0.0.1:8772`.
- A valid iPhone **SRV port must come from mDNS**. Port 8771 is the DropBridge receiver's port and is not necessarily the iPhone's.
- iPhone returned a TLS alert in Q48, but previous logs alone do not establish whether certificate structure, trust chain, protocol negotiation or sender identity caused it.
- OpenDrop generates an RSA 2048-bit self-signed certificate with the device name as CN and historically supported Everyone mode. WinAWDL generates an RSA 2048-bit self-signed certificate with CN=AirDrop receiver. Both disable peer verification in their own client context; **this only affects the client**, not iOS validation of the sender.

## Test prerequisites
On a Windows test host with DropBridge's WinUSB/AWDL chain working, launch the chain with the explicitly opt-in outbound proxy (`--allow-send`); see `docs/SEND_PATH.md`. Enable iPhone AirDrop "Everyone for 10 Minutes", unlock and keep the screen on, and obtain its current IPv6 address **and SRV port** from mDNS discovery logs. Use the existing test environment with a Python SSL runtime.

Run the TLS probe with the same certificate as DropBridge:
```powershell
python tools/probe_send_tls.py --address fe80::YOUR_PHONE_ADDRESS --port YOUR_PHONE_SRV_PORT --keys dist/DropBridge/data/keys
```
Optional one-variable tests (each is diagnostic, not a recommended production setting):
```powershell
python tools/probe_send_tls.py --address fe80::YOUR_PHONE_ADDRESS --port YOUR_PHONE_SRV_PORT --keys dist/DropBridge/data/keys --max-tls 1.2
python tools/probe_send_tls.py --address fe80::YOUR_PHONE_ADDRESS --port YOUR_PHONE_SRV_PORT --keys dist/DropBridge/data/keys --omit-client-cert
```

## Interpretation
- `HANDSHAKE_SUCCEEDED`: TLS transport works with that configuration. It does **not** imply /Discover, /Ask or /Upload works.
- `TLS_ERROR`: preserve the exact SSL reason, library and message. `certificate_unknown` and `unknown_ca` are related but not equivalent; their causes cannot be confirmed from the label alone.
- `NETWORK_OR_PROXY_ERROR`: first confirm the outbound proxy, the mDNS-derived SRV port, AWDL peer status and link-local IPv6 mapping.
- Compare each result against baseline on the *same phone/iOS version*. Never change CN, TLS version, sender identity and flags simultaneously.

## Safety / reproducibility
The probe sends no HTTP request or file and never uploads TLS private keys. Certificate SHA-256 fingerprints are okay to share, but **do not commit** any contents of `data/keys`, exported Apple credentials or raw TLS key logs. Avoid using this experiment against devices without authorization.

## Next source review
Compare OpenDrop `opendrop/config.py`, `opendrop/client.py` against WinAWDL `airdrop/identity.py`, `airdrop/sender.py`, then implement certificate CN as an explicit test variable only after a baseline TLS alert is captured. Keep GPL-3.0 OpenDrop code out of DropBridge's MIT components unless license implications are reviewed.
