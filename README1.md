
## Tick-to-trade data flow
 
```
[ exchange / free API ]
        |  websocket or UDP/multicast (later)
        v
  ( Feed thread, pinned )
    socket read -> decode (simdjson / binary) -> normalize
        |
        v   SPSC ring buffer (lock-free, cache-aligned)
        |
  ( Trading thread, pinned, busy-poll )     <-- HOT PATH starts
    drain ring -> OrderBook.apply()
                -> Strategy.on_event() -> OrderIntent
                -> Risk.check(intent)   -> pass/reject
                -> OMS.register(intent) -> OrderRequest
        |
        v   SPSC ring buffer
        |                                     <-- HOT PATH ends
  ( Gateway thread, pinned )
    encode -> socket write -> exchange
 
  ( Telemetry thread, low priority )
    drains log + metric rings, writes to disk / stdout
```
 
The hot path is the middle block. It touches only preallocated memory and never blocks.
 
## Exchange API specifications

Reference material, not dependencies. No venue protocol is implemented before Phase 6; the
engine runs on the synthetic feed until then. These specifications document the conventions
the core primitives are designed around: fixed-width binary messages rather than JSON,
prices as integers with an implied decimal, sequence numbers with a separate gap-recovery
channel, and an order-entry protocol distinct from the market data feed.

Version numbers are part of most filenames below and the venues revise them in place. A
dead link means a newer revision exists, not that the document was withdrawn.

### NSE India

- [Realtime CM/CD tick-by-tick feed, v6.3](https://nsearchives.nseindia.com/web/sites/default/files/inline-files/Realtime_CM_CD_TBT%20ver%206.3.pdf)
  — TBT multicast feed for the cash and currency derivatives segments.

### Nasdaq

Market data:

- [TotalView-ITCH 5.0](https://www.nasdaqtrader.com/content/technicalsupport/specifications/dataproducts/NQTVITCHSpecification.pdf)
  — fixed-length big-endian binary, full order-by-order depth, prices as 4-byte integers
  with 4 implied decimals. The message set (Add, Execute, Cancel, Delete, Replace) is the
  input alphabet the Phase 3 order book is modelled on.
- [Data product specifications index](https://www.nasdaqtrader.com/Trader.aspx?id=MDDataProducts)
  — landing page, stable across revisions of the individual specifications.

Order entry:

- [OUCH 5.0](https://www.nasdaqtrader.com/content/technicalsupport/specifications/TradingProducts/Ouch5.0.pdf)
  — binary order entry: enter, replace and cancel requests, and the execution messages
  returned. The counterpart to ITCH, and the reference for the Phase 4 gateway.
  ([OUCH landing page](https://www.nasdaqtrader.com/Trader.aspx?id=OUCH))
- [Protocol quick reference](https://www.nasdaqtrader.com/content/ProductsServices/Trading/Protocols_quickref.pdf)
  — one-page comparison of OUCH, [RASH](https://www.nasdaqtrader.com/Trader.aspx?id=RASH)
  and [FIX](https://www.nasdaqtrader.com/Trader.aspx?id=FIX), including the functionality
  each protocol trades away for latency.

### NYSE (Pillar)

Market data:

- [Pillar Integrated Feed client specification, v2.3c](https://www.nyse.com/publicdocs/nyse/data/Pillar_Integrated_Feed_Client_Specification_v2.3c.pdf)
  — order-by-order depth, trades and status on a single feed.
- [Pillar common client specification, v2.6c](https://www.nyse.com/publicdocs/nyse/data/Pillar_Common_Client_Specification_v2.6c.pdf)
  — packet framing, sequencing, retransmission and refresh mechanics shared by every Pillar
  feed. The gap-recovery design lives here rather than in the feed-specific documents.
- [XDP common client specification, v2.4](https://www.nyse.com/publicdocs/nyse/data/XDP_Common_Client_Specification_v2.4.pdf)
  — the earlier XDP generation that Pillar succeeded.

Order entry:

- [Pillar Gateway binary protocol specification](https://www.nyse.com/publicdocs/nyse/NYSE_Pillar_Gateway_Binary_Protocol_Specification.pdf)
  — the low-latency order entry path.
- [Pillar Gateway FIX protocol specification](https://www.nyse.com/publicdocs/nyse/NYSE_Pillar_Gateway_FIX_Protocol_Specification.pdf)
  — the same venue over FIX. The difference against the binary specification measures the
  cost of a self-describing tag=value wire format.
- [Pillar platform overview](https://www.nyse.com/pillar) — index of the above.

### BSE India

BOLT Plus is built on Deutsche Börse's T7 platform, so its interfaces carry T7's names and
message design rather than anything BSE-specific. They form a third protocol family
alongside Nasdaq's and NYSE's.

- [ETI (Enhanced Trading Interface) API manual, v1.4.8](https://www.bseindia.com/downloads1/ETI_API_Manual_1.4.8.pdf)
  — order entry; asynchronous binary request/response over TCP.
- [BSE market data interfaces manual, v1.36](https://www.bseindia.com/downloads1/BSE_market%20data_manual_v%20136.pdf)
  ([v1.35](https://www.bseindia.com/downloads1/BSE_market_data_manual_v135.pdf))
  — EMDI (price-level depth) and EOBI (full order-by-order book), both multicast. EMDI, MDI
  and RDI carry FIX messages in FAST encoding and require a template-driven decoder; EOBI is
  plain binary. The split between the two is a direct illustration of the depth-versus-
  bandwidth tradeoff.
- [BSE Direct NFCAST manual, v5.0](https://www.bseindia.com/downloads1/BSE_DIRECT_NFCAST_Manual.pdf)
  — the lighter broadcast feed.
- [BOLT Plus connectivity manual, v1.12.1](https://www.bseindia.com/downloads1/BOLTPLUS_Connectivity_Manual_V1_12_1.pdf)
  — sessions, multicast addressing and per-segment port layout.
- [BOLT Plus IML API, v6.0](https://www.bseindia.com/downloads1/BOLTPlus_IML_API_version_6.0.pdf)
  — the intermediate message layer above ETI.
