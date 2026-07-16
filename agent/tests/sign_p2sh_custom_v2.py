#!/usr/bin/env python3
"""Manually sign a P2SH custom redeem-script spend (CLTV/CSV style) on Blackcoin."""
import sys, json, hashlib, requests
from ecdsa import SigningKey, SECP256k1, NIST256p
from ecdsa.ellipticcurve import PointJacobi, Point

NODE = {"url": "http://<NODE>:<PORT>/", "user": "<RPCUSER>", "pass": "<RPCPASS>"}
WALLET = "<WALLET_NAME>"
RETURN_ADDR = "<RETURN_ADDRESS>"
FEE = 0.001
AMOUNT = 1.0 - FEE
SIGHASH_ALL = 0x01
SECP_N = 0xFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFEBAAEDCE6AF48A03BBFD25E8CD0364141

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
def b58decode(s):
    num = 0
    for c in s:
        num = num * 58 + B58.index(c)
    data = num.to_bytes((num.bit_length() + 7) // 8, 'big')
    for c in s:
        if c == '1':
            data = b'\x00' + data
        else:
            break
    return data

def wif_to_privkey(wif):
    data = b58decode(wif)
    payload = data[:-4]
    assert data[-4:] == hashlib.sha256(hashlib.sha256(payload).digest()).digest()[:4], "WIF checksum failed"
    assert payload[0] == 0x99, f"WIF version {payload[0]:02x} != 0x99"
    if payload[-1] == 0x01:
        return payload[1:-1], True
    return payload[1:], False

def hash256(data):
    return hashlib.sha256(hashlib.sha256(data).digest()).digest()

def varint(n):
    if n < 0xfd:
        return bytes([n])
    elif n <= 0xffff:
        return b'\xfd' + n.to_bytes(2, 'little')
    elif n <= 0xffffffff:
        return b'\xfe' + n.to_bytes(4, 'little')
    else:
        return b'\xff' + n.to_bytes(8, 'little')

def parse_varint(raw, idx):
    b = raw[idx]
    if b < 0xfd:
        return b, idx+1
    elif b == 0xfd:
        return int.from_bytes(raw[idx+1:idx+3], 'little'), idx+3
    elif b == 0xfe:
        return int.from_bytes(raw[idx+1:idx+5], 'little'), idx+5
    else:
        return int.from_bytes(raw[idx+1:idx+9], 'little'), idx+9

def parse_tx(raw_hex):
    raw = bytes.fromhex(raw_hex)
    idx = 0
    version = int.from_bytes(raw[idx:idx+4], 'little'); idx += 4
    vin_count, idx = parse_varint(raw, idx)
    inputs = []
    for _ in range(vin_count):
        txid = raw[idx:idx+32][::-1].hex(); idx += 32
        vout = int.from_bytes(raw[idx:idx+4], 'little'); idx += 4
        script_len, idx = parse_varint(raw, idx)
        script = raw[idx:idx+script_len].hex(); idx += script_len
        seq = int.from_bytes(raw[idx:idx+4], 'little'); idx += 4
        inputs.append({"txid": txid, "vout": vout, "scriptSig": script, "sequence": seq})
    vout_count, idx = parse_varint(raw, idx)
    outputs = []
    for _ in range(vout_count):
        value = int.from_bytes(raw[idx:idx+8], 'little'); idx += 8
        script_len, idx = parse_varint(raw, idx)
        script = raw[idx:idx+script_len].hex(); idx += script_len
        outputs.append({"value": value, "scriptPubKey": script})
    locktime = int.from_bytes(raw[idx:idx+4], 'little'); idx += 4
    return {"version": version, "inputs": inputs, "outputs": outputs, "locktime": locktime}

def serialize_input(inp):
    out = b''
    out += bytes.fromhex(inp["txid"])[::-1]
    out += inp["vout"].to_bytes(4, 'little')
    script = bytes.fromhex(inp["scriptSig"])
    out += varint(len(script))
    out += script
    out += inp["sequence"].to_bytes(4, 'little')
    return out

def serialize_output(outp):
    out = b''
    out += outp["value"].to_bytes(8, 'little')
    script = bytes.fromhex(outp["scriptPubKey"])
    out += varint(len(script))
    out += script
    return out

def serialize_tx(tx):
    out = b''
    out += tx["version"].to_bytes(4, 'little')
    out += varint(len(tx["inputs"]))
    for inp in tx["inputs"]:
        out += serialize_input(inp)
    out += varint(len(tx["outputs"]))
    for outp in tx["outputs"]:
        out += serialize_output(outp)
    out += tx["locktime"].to_bytes(4, 'little')
    return out

def legacy_sighash(tx, input_idx, redeem_script_hex):
    tx2 = {"version": tx["version"], "locktime": tx["locktime"],
           "inputs": [], "outputs": tx["outputs"]}
    for i, inp in enumerate(tx["inputs"]):
        if i == input_idx:
            new_inp = dict(inp)
            new_inp["scriptSig"] = redeem_script_hex
            tx2["inputs"].append(new_inp)
        else:
            new_inp = dict(inp)
            new_inp["scriptSig"] = ""
            tx2["inputs"].append(new_inp)
    serialized = serialize_tx(tx2) + SIGHASH_ALL.to_bytes(4, 'little')
    return hash256(serialized)

def der_sig(r, s):
    # Enforce low-S
    if s > SECP_N // 2:
        s = SECP_N - s
    def encode_int(x):
        b = x.to_bytes((x.bit_length() + 7) // 8, 'big')
        if b[0] & 0x80:
            b = b'\x00' + b
        return bytes([len(b)]) + b
    rb = encode_int(r)
    sb = encode_int(s)
    total = b'\x02' + rb + b'\x02' + sb
    return bytes([0x30, len(total)]) + total

def sign_with_privkey(wif, tx, input_idx, redeem_hex):
    privkey, compressed = wif_to_privkey(wif)
    sk = SigningKey.from_string(privkey, curve=SECP256k1)
    digest = legacy_sighash(tx, input_idx, redeem_hex)
    sig = sk.sign_digest(digest, sigencode=lambda r,s,order: der_sig(r, s))
    sig_with_hashtype = sig + bytes([SIGHASH_ALL])
    return sig_with_hashtype.hex()

def build_signed_tx(tx, input_idx, sig_hex, redeem_hex):
    tx2 = dict(tx)
    tx2["inputs"] = [dict(inp) for inp in tx["inputs"]]
    sig = bytes.fromhex(sig_hex)
    redeem = bytes.fromhex(redeem_hex)
    scriptSig = varint(len(sig)) + sig + varint(len(redeem)) + redeem
    tx2["inputs"][input_idx]["scriptSig"] = scriptSig.hex()
    return serialize_tx(tx2).hex()

def main():
    setup = json.load(open("/tmp/blackmore_scripttest_legacy_setup.json"))
    tests = {
        "cltv": {
            "txid": "<CLTV_FUNDING_TXID>",
            "vout": 0,
            "redeem": setup["cltv"]["redeem_script_hex"],
            "sequence": 0xFFFFFFFE,
            "locktime": setup["cltv"]["lock_height"],
            "wif": rpc_wallet("dumpprivkey", [setup["cltv"]["pubkey_address"]]),
        },
        "csv": {
            "txid": "<CSV_FUNDING_TXID>",
            "vout": 0,
            "redeem": setup["csv"]["redeem_script_hex"],
            "sequence": 0,
            "locktime": 0,
            "wif": rpc_wallet("dumpprivkey", [setup["csv"]["pubkey_address"]]),
        },
    }

    final_txs = {}

    for name, cfg in tests.items():
        print(f"\n=== {name.upper()} ===")
        inputs = [{"txid": cfg["txid"], "vout": cfg["vout"], "sequence": cfg["sequence"]}]
        outputs = [{RETURN_ADDR: AMOUNT}]
        raw_hex = rpc_wallet("createrawtransaction", [inputs, outputs, cfg["locktime"]])
        print(f"Raw tx: {raw_hex}")

        tx = parse_tx(raw_hex)
        sig_hex = sign_with_privkey(cfg["wif"], tx, 0, cfg["redeem"])
        print(f"Signature: {sig_hex}")

        signed_hex = build_signed_tx(tx, 0, sig_hex, cfg["redeem"])
        print(f"Signed tx: {signed_hex}")

        test = rpc("testmempoolaccept", [[signed_hex]])
        print(f"Mempool accept: {test[0]['allowed']}")
        if not test[0]['allowed']:
            print(f"  reject-reason: {test[0].get('reject-reason', 'unknown')}")
        else:
            print(f"  vsize: {test[0]['vsize']}, fees: {test[0]['fees']}")
            final_txs[name] = signed_hex

    with open("/tmp/blackmore_scripttest_manual_signed_v2.json", "w") as f:
        json.dump(final_txs, f, indent=2)

    print("\n=== Ready to broadcast ===")
    for name, hextx in final_txs.items():
        print(f"{name}: {hextx}")

if __name__ == "__main__":
    main()
