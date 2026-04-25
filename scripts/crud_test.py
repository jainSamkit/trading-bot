#!/usr/bin/env python3
"""
Independent CRUD test for Delta Exchange testnet.
Sends create → edit → cancel and prints REST responses.
Run this while oms_test is running to verify the WS channel picks up the changes.

Usage:
    pip install requests
    python3 scripts/crud_test.py
"""

import hashlib
import hmac
import json
import time
import requests

# ─── config ───────────────────────────────────────────────────────────────────

REST_HOST  = "https://cdn-ind.testnet.deltaex.org"
API_KEY    = "JiV80pv3OJKlUyisMN2x4BHqnKONW5"
API_SECRET = "ELUujrDwVJrK5UiJBkQyBKTJNdVvOOy8OKUuEsjxaeeLWXai6emZYsCFtt40"
SYMBOL     = "SOLUSD"

# ─── auth ─────────────────────────────────────────────────────────────────────

def sign(method: str, path: str, query: str, body: str) -> dict:
    ts = str(int(time.time() * 1000))
    payload = method + ts + path + query + body
    sig = hmac.new(
        API_SECRET.encode(), payload.encode(), hashlib.sha256
    ).hexdigest()
    return {
        "api-key":    API_KEY,
        "signature":  sig,
        "timestamp":  ts,
        "Content-Type": "application/json",
    }

def get(path: str, params: dict = None) -> dict:
    query = "&".join(f"{k}={v}" for k, v in (params or {}).items())
    r = requests.get(
        REST_HOST + path,
        params=params,
        headers=sign("GET", path, query, ""),
        timeout=10,
    )
    return r.json()

def post(path: str, body: dict) -> dict:
    body_str = json.dumps(body)
    r = requests.post(
        REST_HOST + path,
        data=body_str,
        headers=sign("POST", path, "", body_str),
        timeout=10,
    )
    return r.json()

def put(path: str, body: dict) -> dict:
    body_str = json.dumps(body)
    r = requests.put(
        REST_HOST + path,
        data=body_str,
        headers=sign("PUT", path, "", body_str),
        timeout=10,
    )
    return r.json()

def delete(path: str, body: dict) -> dict:
    body_str = json.dumps(body)
    r = requests.delete(
        REST_HOST + path,
        data=body_str,
        headers=sign("DELETE", path, "", body_str),
        timeout=10,
    )
    return r.json()

# ─── helpers ──────────────────────────────────────────────────────────────────

def get_product_id(symbol: str) -> int:
    data = get(f"/v2/products/{symbol}")
    assert data["success"], f"product fetch failed: {data}"
    return data["result"]["id"]

def print_order(label: str, resp: dict):
    if not resp.get("success"):
        print(f"  [{label}] ERROR: {resp}")
        return
    o = resp["result"]
    print(f"  [{label}] id={o['id']}  state={o['state']}  "
          f"px={o.get('limit_price','?')}  size={o['size']}  "
          f"filled={o['filled_size']}  ts={o.get('timestamp','?')}")

# ─── tests ────────────────────────────────────────────────────────────────────

def test_crud():
    print("=== fetching product ===")
    product_id = get_product_id(SYMBOL)
    print(f"  {SYMBOL}  product_id={product_id}")

    # ── CREATE ────────────────────────────────────────────────────────────────
    print("\n=== CREATE (far OTM limit buy, will not fill) ===")
    resp = post("/v2/orders", {
        "order_type":    "limit_order",
        "side":          "buy",
        "product_id":    product_id,
        "size":          1,
        "limit_price":   "50",
        "time_in_force": "gtc",
        "post_only":     "true",
        "reduce_only":   "false",
        "order_source":  "place_order",
        "source":        "api",
    })
    print_order("create", resp)
    if not resp.get("success"):
        return

    order_id = resp["result"]["id"]
    time.sleep(1)  # let WS echo arrive

    # ── EDIT ─────────────────────────────────────────────────────────────────
    print("\n=== EDIT (move price to 900) ===")
    resp = put(f"/v2/orders/{order_id}", {
        "id":          order_id,
        "product_id":  product_id,
        "limit_price": "45",
        "order_source": "open_order_edit",
    })
    print_order("edit", resp)
    time.sleep(1)

    # ── READ (verify) ─────────────────────────────────────────────────────────
    print("\n=== READ (verify edit applied) ===")
    resp = get(f"/v2/orders/{order_id}")
    print_order("read", resp)

    # ── CANCEL ───────────────────────────────────────────────────────────────
    print("\n=== CANCEL ===")
    resp = delete(f"/v2/orders/{order_id}", {
        "id":         order_id,
        "product_id": product_id,
    })
    print_order("cancel", resp)
    time.sleep(1)

    # ── READ OPEN ORDERS (should be empty or no matching id) ─────────────────
    print("\n=== OPEN ORDERS (verify cancelled) ===")
    resp = get("/v2/orders", {"states": "open", "page_size": "5"})
    if resp.get("success"):
        orders = resp.get("result", [])
        print(f"  open orders: {len(orders)}")
        for o in orders:
            print(f"    id={o['id']}  px={o.get('limit_price','?')}  sym={o.get('product_symbol','?')}")
    else:
        print(f"  ERROR: {resp}")

    # ── POSITIONS ─────────────────────────────────────────────────────────────
    print("\n=== POSITIONS ===")
    resp = get("/v2/positions/margined")
    if resp.get("success"):
        positions = [p for p in resp.get("result", []) if p["size"] != 0]
        print(f"  non-zero positions: {len(positions)}")
        for p in positions:
            print(f"    sym={p['product_symbol']}  size={p['size']}  "
                  f"entry={p['entry_price']}  margin={p['margin']}")
    else:
        print(f"  ERROR: {resp}")

    # ── WALLET ────────────────────────────────────────────────────────────────
    print("\n=== WALLET (USD) ===")
    resp = get("/v2/wallet/balances")
    if resp.get("success"):
        usd = next((w for w in resp["result"] if w["asset_symbol"] == "USD"), None)
        if usd:
            print(f"  balance={usd['balance']}  available={usd['available_balance']}  "
                  f"pos_margin={usd['position_margin']}  ord_margin={usd['order_margin']}")
    else:
        print(f"  ERROR: {resp}")

    print("\n=== CRUD test complete ===")


if __name__ == "__main__":
    test_crud()
