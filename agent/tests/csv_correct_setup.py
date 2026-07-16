#!/usr/bin/env python3
"""Set up a CORRECT minimal-encoding CSV P2SH test on Blackcoin v28.4.0."""
import requests, json, hashlib

NODE = {"url": "http://<NODE>:<PORT>/", "user": "<RPCUSER>", "pass": "<RPCPASS>"}
WALLET = "<WALLET_NAME>"
CSV_DELAY = 1  # real relative delay of 1 block

def rpc(method, params=[], wallet=None):
    url = NODE["url"] if not wallet else f"{NODE['url'].rstrip('/')}/wallet/{wallet}"
    r = requests.post(url, auth=(NODE["user"], NODE["pass"]),
                      json={"jsonrpc":"1.0","id":"1","method":method,"params":params},
                      headers={"content-type":"text/plain;"}, timeout=60)
    r.raise_for_status()
    j = r.json()
    if j.get("error"):
        raise RuntimeError(f"{method} {params}: {j['error']}")
    return j["result"]

def rpc_wallet(method, params=[]):
    return rpc(method, params, wallet=WALLET)

B58 = '123456789ABCDEFGHJKLMNPQRSTUVWXYZabcdefghijkmnopqrstuvwxyz'
def b58encode(data):
    num = int.from_bytes(data, 'big')
    result = ''
    while num > 0:
        num, rem = divmod(num, 58)
        result = B58[rem] + result
    for b in data:
        if b == 0:
            result = '1' + result
        else:
            break
    return result

def p2sh_address(redeem_hex, version=85):
    h160 = hashlib.new('ripemd160', hashlib.sha256(bytes.fromhex(redeem_hex)).digest()).digest().hex()
    payload = bytes([version]) + bytes.fromhex(h160)
    checksum = hashlib.sha256(hashlib.sha256(payload).digest()).digest()[:4]
    return b58encode(payload + checksum)

print("Generating fresh key for correct CSV test...")
addr = rpc_wallet("getnewaddress", ["csv_correct", "legacy"])
info = rpc_wallet("getaddressinfo", [addr])
pubkey = info["pubkey"]
print(f"  pubkey address: {addr}")
print(f"  pubkey:         {pubkey}")

# Minimal encoding for delay=1: OP_1 = 0x51
redeem_hex = f"51b27521{pubkey}ac"
print(f"\nCorrect CSV redeem script: {redeem_hex}")

# Verify with decodescript
dec = rpc("decodescript", [redeem_hex])
print(f"Decoded asm: {dec['asm']}")

p2sh = p2sh_address(redeem_hex)
print(f"P2SH funding address: {p2sh}")

# Import as watch-only
rpc_wallet("importaddress", [p2sh, "csv_correct", False])
print("Imported as watch-only.")

# Save setup
output = {
    "wallet": WALLET,
    "node": NODE["url"],
    "csv_correct": {
        "delay": CSV_DELAY,
        "pubkey": pubkey,
        "pubkey_address": addr,
        "redeem_script_hex": redeem_hex,
        "p2sh_address": p2sh,
        "note": "Uses minimal OP_1 encoding for delay=1. Spend with nSequence=1 after 1 confirmation."
    }
}

with open("/tmp/blackmore_csv_correct_setup.json", "w") as f:
    json.dump(output, f, indent=2)

with open("agent/tests/blackmore_csv_correct_setup.json", "w") as f:
    json.dump(output, f, indent=2)

print("\n" + "="*60)
print("SEND FUNDS TO THIS ADDRESS (mainnet):")
print(f"  {p2sh}")
print("="*60)
print(f"Suggested amount: 0.001 BLK or 1 BLK")
print(f"After the funding tx has 1 confirmation, the spend script can be run.")
print("Setup saved to /tmp/blackmore_csv_correct_setup.json")
print("Setup copied to agent/tests/blackmore_csv_correct_setup.json")
