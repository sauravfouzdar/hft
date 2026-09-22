
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

1. NSE India: [https://nsearchives.nseindia.com/web/sites/default/files/inline-files/Realtime_CM_CD_TBT%20ver%206.3.pdf](NSE API Spec)
