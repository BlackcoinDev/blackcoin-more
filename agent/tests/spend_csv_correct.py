#!/usr/bin/env python3
"""Spend the correctly-encoded CSV P2SH test output."""
import sys, json, hashlib, requests
from ecdsa import SigningKey, SECP256k1

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
    for c in s: num = num*58 + B58.index(c)
    data = num.to_bytes((num.bit_length()+7)//8, 'big')
    for c in s:
        if c == '1': data = b'\x00'+data
        else: break
    return data

def wif_to_privkey(wif):
    data = b58decode(wif)
    assert data[-4:] == hashlib.sha256(hashlib.sha256(data[:-4]).digest()).digest()[:4]
    assert data[0] == 0x99
    payload = data[:-4]
    if payload[-1] == 0x01:
        return payload[1:-1], True
    return payload[1:], False

def varint(n):
    if n < 0xfd: return bytes([n])
    elif n <= 0xffff: return b'\xfd'+n.to_bytes(2,'little')
    elif n <= 0xffffffff: return b'\xfe'+n.to_bytes(4,'little')
    else: return b'\xff'+n.to_bytes(8,'little')

def parse_varint(raw,idx):
    b = raw[idx]
    if b < 0xfd: return b, idx+1
    if b == 0xfd: return int.from_bytes(raw[idx+1:idx+3],'little'), idx+3
    if b == 0xfe: return int.from_bytes(raw[idx+1:idx+5],'little'), idx+5
    return int.from_bytes(raw[idx+1:idx+9],'little'), idx+9

def parse_tx(h):
    raw = bytes.fromhex(h); idx=0
    ver = int.from_bytes(raw[idx:idx+4],'little'); idx+=4
    n,idx = parse_varint(raw,idx)
    inputs=[]
    for _ in range(n):
        txid = raw[idx:idx+32][::-1].hex(); idx+=32
        vout = int.from_bytes(raw[idx:idx+4],'little'); idx+=4
        sl,idx = parse_varint(raw,idx)
        script = raw[idx:idx+sl].hex(); idx+=sl
        seq = int.from_bytes(raw[idx:idx+4],'little'); idx+=4
        inputs.append({'txid':txid,'vout':vout,'scriptSig':script,'sequence':seq})
    n,idx = parse_varint(raw,idx)
    outputs=[]
    for _ in range(n):
        val = int.from_bytes(raw[idx:idx+8],'little'); idx+=8
        sl,idx = parse_varint(raw,idx)
        script = raw[idx:idx+sl].hex(); idx+=sl
        outputs.append({'value':val,'scriptPubKey':script})
    lt = int.from_bytes(raw[idx:idx+4],'little')
    return {'version':ver,'inputs':inputs,'outputs':outputs,'locktime':lt}

def serialize_tx(tx):
    out = tx['version'].to_bytes(4,'little')
    out += varint(len(tx['inputs']))
    for inp in tx['inputs']:
        out += bytes.fromhex(inp['txid'])[::-1]
        out += inp['vout'].to_bytes(4,'little')
        script = bytes.fromhex(inp['scriptSig'])
        out += varint(len(script)) + script
        out += inp['sequence'].to_bytes(4,'little')
    out += varint(len(tx['outputs']))
    for outp in tx['outputs']:
        out += outp['value'].to_bytes(8,'little')
        script = bytes.fromhex(outp['scriptPubKey'])
        out += varint(len(script)) + script
    out += tx['locktime'].to_bytes(4,'little')
    return out

def legacy_sighash(tx, idx, redeem):
    inputs=[]
    for i,inp in enumerate(tx['inputs']):
        ni = dict(inp)
        ni['scriptSig'] = redeem if i==idx else ''
        inputs.append(ni)
    tx2 = {'version':tx['version'],'locktime':tx['locktime'],'inputs':inputs,'outputs':tx['outputs']}
    return hashlib.sha256(hashlib.sha256(serialize_tx(tx2)+SIGHASH_ALL.to_bytes(4,'little')).digest()).digest()

def der_sig(r,s):
    if s > SECP_N//2: s = SECP_N - s
    def enc(x):
        b = x.to_bytes((x.bit_length()+7)//8,'big')
        return (b'\x00'+b if b[0]&0x80 else b)
    rb, sb = b'\x02'+bytes([len(enc(r))])+enc(r), b'\x02'+bytes([len(enc(s))])+enc(s)
    total = rb+sb
    return b'\x30'+bytes([len(total)])+total

def sign(wif, tx, idx, redeem):
    priv,_ = wif_to_privkey(wif)
    sk = SigningKey.from_string(priv, curve=SECP256k1)
    digest = legacy_sighash(tx, idx, redeem)
    sig = sk.sign_digest(digest, sigencode=lambda r,s,order: der_sig(r,s))
    return (sig + bytes([SIGHASH_ALL])).hex()

def build(tx, idx, sig_hex, redeem_hex):
    tx2 = dict(tx)
    tx2['inputs'] = [dict(inp) for inp in tx['inputs']]
    sig = bytes.fromhex(sig_hex)
    redeem = bytes.fromhex(redeem_hex)
    scriptSig = varint(len(sig)) + sig + varint(len(redeem)) + redeem
    tx2['inputs'][idx]['scriptSig'] = scriptSig.hex()
    return serialize_tx(tx2).hex()

setup = json.load(open("/tmp/blackmore_csv_correct_setup.json"))
redeem = setup['csv_correct']['redeem_script_hex']
pubkey_addr = setup['csv_correct']['pubkey_address']
wif = rpc_wallet("dumpprivkey", [pubkey_addr])
funding_txid = "<CSV_CORRECT_FUNDING_TXID>"
funding_vout = 1

print(f"CSV redeem script: {redeem}")
print(f"Funding txid/vout: {funding_txid}/{funding_vout}")
print(f"Private key fetched for {pubkey_addr}")

# Build raw spend with nSequence=1
raw = rpc_wallet("createrawtransaction", [[{"txid":funding_txid,"vout":funding_vout,"sequence":1}], [{RETURN_ADDR: AMOUNT}], 0])
print(f"\nRaw tx: {raw}")

tx = parse_tx(raw)
sig_hex = sign(wif, tx, 0, redeem)
print(f"Signature: {sig_hex}")

signed = build(tx, 0, sig_hex, redeem)
print(f"Signed tx: {signed}")

# Test mempool acceptance
test = rpc("testmempoolaccept", [[signed]])
print(f"\nMempool accept: {test[0]['allowed']}")
if not test[0]['allowed']:
    print(f"  reject-reason: {test[0].get('reject-reason', 'unknown')}")
    sys.exit(1)

print(f"  vsize: {test[0]['vsize']}, fees: {test[0]['fees']}")

# Broadcast
print("\nBroadcasting...")
txid = rpc("sendrawtransaction", [signed])
print(f"Broadcast txid: {txid}")

# Save result
result = {
    "funding_txid": funding_txid,
    "funding_vout": funding_vout,
    "spending_txid": txid,
    "spending_hex": signed,
    "redeem_script": redeem,
    "nSequence": 1,
    "fee": FEE,
    "return_address": RETURN_ADDR
}
with open("/tmp/blackmore_csv_correct_result.json", "w") as f:
    json.dump(result, f, indent=2)
with open("agent/tests/blackmore_csv_correct_result.json", "w") as f:
    json.dump(result, f, indent=2)

print("\nResult saved to /tmp/blackmore_csv_correct_result.json")
print("Result copied to agent/tests/blackmore_csv_correct_result.json")
