# Parts order rev B: Option J (thumbstick) + Option D (5-way) prototyping (2026-11-08)

> **Corrected 2026-11-15.** Option D (Adafruit 504 D-pad) is the build; Option J is parked, so its lines are deferred (qty 0).
> Fixes from the Rev B review: AWHW header qty 1 → 2; added the C317 10 nF line; screws now match the case-revB1 BOM
> (2× M2×14 SHCS, 2× M2×14 countersunk, 1× M2×6 SHCS); M2×10 dropped; the 0.8 mm nylon washers are not used by case B.1.
> IDC cable qty 1 → 2. The case is ordered only after the Waveshare and the cable are measured.

Supplier order of preference: DigiKey Canada, then Mouser Canada. Items you already own are left out.

## Read first: what could and could not be verified

- **DigiKey prices, stock and `-ND` part numbers are still TBV.** digikey.ca (also .com, .ch and the punchouttest mirrors)
  draws price, stock and the DigiKey PN with JavaScript, and the fetch tool gets only the page shell. Octopart and
  Findchips are blocked for this session (Findchips returned an error page), no Chrome browser is connected, and
  direct curl is refused by the egress proxy. Each DigiKey URL below is a real listing, so the part is carried. The
  product ID in a URL is not the `-ND` part number.
- **mouser.ca pages loaded only for URLs that were already known** (RKJXV122400R). For THB001P and Adafruit 504, the
  figures come from **mouser.com in USD** and are labelled USD.
- **Alps SKQUCAA010 and SKQUAAA010 are both discontinued.** element14 lists both as "No Longer Manufactured". The Alps
  EOL notice (Farnell 3565107.pdf) set the last-time buy for SKQUCAA010 at 2021-09-30 and names **SKRHAAE010** (SMD) as
  its replacement. Neither part is sold by DigiKey or Mouser. **Adafruit 504 is still sold by both**, so it is the
  Option D buy.

## Order table

| # | Item | Mfr | MPN | Supplier | Supplier PN | URL | Qty | Unit price | Stock | Datasheet / key spec |
|---|------|-----|-----|----------|-------------|-----|-----|-----------|-------|----------------------|
| 1 | Thumbstick with push (Option J) **DEFERRED: J parked** | C&K | THB001P | DigiKey CA | TBV | https://www.digikey.ca/en/products/detail/c-k/THB001P/11687191 | 0 (was 2) | TBV (Mouser.com: **US$3.52** @1, US$3.16 @5) | DigiKey TBV; Mouser.com 5,893, 14 wk lead | https://www.littelfuse.com/assetdocs/littelfuse-c-k-rotary-thb-series-datasheet?assetguid=e3458825-585e-4962-9461-54efaec08a2f (2 × 10 kΩ, centre push, THT) |
| 1-M | Same, Mouser alternative | C&K | THB001P | Mouser | 611-THB001P | https://www.mouser.com/en/ProductDetail/CK/THB001P?qs=OlC7AqGiEDlMaVQ%2FEBrivA%3D%3D | (2) | **US$3.52** | 5,893 | as above |
| 1a | Thumbstick cap | — | — | — | — | — | 0 | — | — | No separate cap found. Mouser, SnapEDA and DigiKey list no accessory. Datasheet shows a 7.75 mm plastic shaft; whether a cap is needed or sold separately is TBV |
| 2 | Thumbstick fallback **DEFERRED: J parked** | Alps Alpine | RKJXV122400R | Mouser CA | 688-RKJXV122400R | https://www.mouser.ca/ProductDetail/Alps-Alpine/RKJXV122400R/?qs=GedFDFLaBXEbB%252ByoOVwLBg%3D%3D | (2, only if needed) | **CA$4.72** @1 | 7,966, 19 wk lead | https://www.mouser.com/datasheet/2/15/RKJXK-1370937.pdf (23° max, 10 kΩ). Not on DigiKey |
| 3 | 5-way nav switch, THT (Option D) | Adafruit | 504 | DigiKey CA | TBV (Adafruit parts are `1528-…-ND`, exact PN TBV) | https://www.digikey.ca/en/products/detail/adafruit-industries-llc/504/26735579 | 2 | TBV (Mouser.com: **US$1.95**) | DigiKey "ships today", qty TBV; Mouser.com 225 | SKQUCAA010 drawing: https://core-electronics.com.au/attachments/localcontent/SKQUCAA010-ALPS_1857261b06a.pdf |
| 3-M | Same, Mouser alternative | Adafruit | 504 | Mouser | 485-504 | https://www.mouser.com/ProductDetail/485-504 | (2) | **US$1.95** | 225, 1 wk lead | as above |
| 4 | Knob Ø19.05, black matte, no indicator, 6 mm bore | Kilo International | OEJNI-75-4-7 | DigiKey CA | TBV | https://www.digikey.ca/en/products/detail/kilo-international/OEJNI-75-4-7/5970395 | 2 | TBV | TBV | Listing: "Knurled, Diamond Knob 0.236" (6.00mm) Shaft … Metal Black Matte". Not found at Mouser |
| 5 | Pre-made 34-pos IDC cable, socket to socket, 6" (152.4 mm), grey, with strain relief and polarising key | Assmann WSW | H3DDS-3406G | DigiKey CA | TBV (search `H3DDS-3406G`) | TBV (search H3DDS-3406G on digikey.ca; DigiKey description "IDC CBL - HHKR34S/AE34G/HHKR34S") | 2 (one spare; measure the mated header + plug height when it arrives, before the case order: case `cp_hdr_h` assumes 9.0, the strain relief may make it ~12) | TBV | TBV | Replaces H3DDH-3406G (discontinued). Same HHKR34 socket with strain relief + key and AE34G grey ribbon; only the contact plating differs (H3DDH = HHKR34H gold, H3DDS = HHKR34S tin). 2.54 mm connector pitch, 1.27 mm cable pitch. Length chart: media.digikey.com/pdf/Data%20Sheets/Digi-Key%20PDFs/Ribbon_Cable_Assembly_Length.pdf |
| 6 | 2×17 shrouded box header, THT, straight | Assmann WSW | AWHW-34G-0202-T | DigiKey CA | TBV | https://www.digikey.ca/en/products/detail/assmann-wsw-components/AWHW-34G-0202-T/5224934 | 2 (one per control PCB built, plus a spare / second D board) | TBV | TBV | "Connector Header Through Hole 34 position 0.100" (2.54mm)" |
| 7 (alt to 5) | IDC socket 34-pos, centre-bump key, **with strain relief** | 3M | 89134-0103 | DigiKey CA | TBV | https://www.digikey.ca/en/products/detail/3m/89134-0103/2767072 | (2) | TBV | TBV | 3M 891 series: suffix "-0103" = centre-bump key + strain relief (https://docs.rs-online.com/208d/0900766b813e073f.pdf) |
| 7b (alt to 5) | 34-way 1.27 mm grey ribbon, ~150 mm | 3M (e.g. 3302 series) | TBV | DigiKey CA | TBV | — | (1 ft) | TBV | TBV | Not looked up in detail. Needed only if you crimp your own instead of buying #5 |
| 8 | M2 nylon washer, 0.8 mm thick **OPTIONAL: not used by case B.1** | Essentra | 16M02050080 | DigiKey CA | TBV | https://www.digikey.ca/en/products/detail/essentra-components/16M02050080/10299172 | 0 (was 20) | TBV | TBV | "M2 Flat Washer 0.031" (0.80mm) Thick Nylon". OD probably 5.0 mm (read from the PN, TBV). Thinner option: Essentra MFW010A, 0.30 mm (https://www.digikey.ca/en/products/detail/essentra-components/MFW010A/4105387) |
| 9 | M2 heat-set insert, brass, M2 × 4 mm, Ø3.2 mm hole | ruthex | RX-M2x4 (GE-M2x04-001), 70 pcs | **Amazon.ca fallback** (none found at DigiKey or Mouser) | ASIN TBV | Spec reference: https://www.3djake.com/ruthex/threaded-insert-m2-70-pieces | 1 pack | TBV (3DJake US$8.36 ex-VAT, reference only) | TBV | L 4.0 mm, OD 3.6 mm, hole Ø3.2 mm (3DJake table, columns not labelled) |
| 10 | M2 × 6 socket-head cap screw (ISO 4762), H3 | generic | — | **Amazon.ca or Bambu Lab CA store fallback** (no DigiKey or Mouser listing found) | — | https://us.store.bambulab.com/products/m2-socket-head-cap-machine-screws-shcs | 1 per device (pack of 20) | TBV | TBV | Bambu: 4.8-grade carbon steel, 4–30 mm lengths, packs of 20 up to 16 mm |
| 11 | M2 × 14 socket-head cap screw (ISO 4762), H1/H2 through back shell + PCB | generic | — | as #10 | — | as #10 | 2 per device (pack of 20) | TBV | TBV | Replaces the unused M2 × 10. Bambu packs go up to 16 mm |
| 12 | M2 × 14 countersunk screw (ISO 10642), back-shell top corners | generic | — | Amazon.ca fallback | — | — | 2 per device (pack) | TBV | TBV | Head flush in the back shell; into the front-shell inserts |
| 13 | 10 nF 50 V X7R radial, 5.08 mm leads | KEMET | C317C103K5R5TA | DigiKey CA | TBV (search `C317C103K5R5TA`) | — | 2 spare (D needs 10, 10 are owned; +10 per extra D board; +8 if J is revived) | TBV | TBV | Stand upright on the bottom side |

## Totals

- **Verified prices only:** Adafruit 504 2 × US$1.95 = **US$3.90 (Mouser.com, USD)**. (THB001P, 2 × US$3.52, is deferred with Option J.)
  Subtotal, before shipping, duty and FX.
- Every other line (Kilo knobs, IDC cable, box header, washers, inserts, screws) is **TBV**, so no grand total is given.
  Get it by bulk-adding `digikey-revB.csv` into a digikey.ca cart.
- Fallback RKJXV122400R, if used: 2 × CA$4.72 = CA$9.44 at mouser.ca.

## Notes

1. **Breadboard test for THB001P.** No breakout board exists for THB001P. The Adafruit 3246 "Analog Mini Thumbstick
   Breakout" is for Adafruit 2765, and SparkFun's breakout is for the PS2-style stick. Neither has a THB footprint. Pin
   pitch could not be read from any reachable copy of the datasheet: the drawing is an image, and ckswitches.com
   redirects. Prior research found 6 × Ø1.0 signal holes, 4 × Ø1.2 and 4 × Ø1.5 mounting holes, which is **not
   assumed to be a 2.54 mm grid (TBV)**. Plan to **hand-wire it**: solder 26–30 AWG wires to the 6 signal pins
   (X, Y wipers and ends, push) and run them to a 2.54 mm header strip on the breadboard. Stand the part in a scrap of
   perfboard drilled to fit, or tack it to a 3D-printed jig. SnapEDA has the footprint
   (https://www.snapeda.com/parts/THB001P/C&K/view-part/) for printing a 1:1 drilling template.
2. **Breadboard test for Adafruit 504.** Pins are slightly off the 0.1" grid. Adafruit and Little Bird say it presses
   into a breadboard with slight bending of the leads. Common pin goes to GND; the other 5 go to pull-ups.
3. **Option D long-term risk.** Adafruit 504 is built on the discontinued SKQUCAA010, so stock is whatever Adafruit
   still has (Mouser 225, Little Bird 100). For a production board, the active Alps successor is **SKRHAAE010** (SMD,
   DigiKey listing https://www.digikey.cn/en/products/detail/alps-alpine/SKRHAAE010/19529098; LCSC C202423), or
   SKRHABE010 with guide bosses. It is not on this prototyping order.
4. **IDC link.** H3DDS-3406G (replaces the discontinued H3DDH-3406G) has the same socket type on both ends, with strain relief and a polarising key. The key
   seats in the AWHW shroud notch on the control PCB. The Waveshare's 34-pin male header is unshrouded, so check the
   pin-1 (red stripe) orientation there by hand. Pin-1-to-pin-1 mapping on a socket-to-socket ribbon depends on both
   sockets facing the same way: confirm it with a continuity check (TBV). Consider ordering 2 cables as a spare.
5. **Fasteners.** No M2 heat-set inserts or M2 socket-cap screws were found at DigiKey or Mouser (McMaster excluded as
   requested). Essentra nylon washers are on DigiKey. Inserts and screws fall back to Amazon.ca (ruthex RX-M2x4 70-pack;
   M2 × 6 and M2 × 14 SHCS packs, M2 × 14 countersunk) or the Bambu Lab Canada store.

## Sources
- SKQUCAA010 NLM: https://my.element14.com/alps/skqucaa010/switch-5-way-0-05a-12vdc-tht/dp/1435775 ; EOL notice: https://www.farnell.com/datasheets/3565107.pdf
- SKQUAAA010 NLM: https://my.element14.com/alps/skquaaa010/navigation-switch-4way/dp/1435773
- Adafruit 504 breadboard note: https://core-electronics.com.au/thru-hole-5-way-navigation-switch.html.plain.md ; https://littlebirdelectronics.com.au/products/thru-hole-5-way-navigation-switch.json
- H3DDS-3406G specs: https://www.hotenda.com/product-detail/assmann-wsw-components-484/h3dds-3406g ; https://ichome.com/product-detail/assmann-wsw-components/h3dds-3406g (old H3DDH-3406G = HHKR34H/AE34G/HHKR34H: https://zenode.ai/parts/jp6w2re2qx)
- DigiKey `MPN-ND` URL pattern: https://www.digikey.ca/en/products/detail/H3CCS-3406G/H3CCS-3406G-ND/1218593
