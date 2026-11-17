# Default library (approximate)

`library.json` seeds a new user's parts library: typical insulated outer diameters for
TXL, GXL, SXL (SAE J1128) and FLRY-B (ISO 6722) wire, and common sleeving sizes.

**These numbers are approximate.** They are reasonable starting points, not datasheet
values. Every entry is marked `"approximate": true` and the app labels it as such until
the user edits it. Verify against your manufacturer's datasheets before relying on bundle
diameters or sleeving recommendations.

## Connector parts (unverified)

`connectorParts` and `pinParts` seed housings, terminals and cavity plugs for:

- **TE Deutsch DT** (size 16): DT04 receptacles / DT06 plugs, 2, 3, 4, 6, 8 and 12 way;
  solid and stamped pins and sockets, 114017 sealing plug.
- **TE Deutsch DTM** (size 20): DTM04 / DTM06, 2 to 12 way; solid contacts, 0413-204-2005 plug.
- **TE Deutsch DTP** (size 12): DTP04 / DTP06, 2 and 4 way; solid contacts, 114017 plug.
- **TE AMPSEAL**: 770680-1 (23 way) and 776164-1 (35 way) plugs; 770520-1 / -3 sockets,
  770678-1 cavity plug.
- **Molex CMC**: 64323-1029 and 64322-1039 terminals and 64325 cavity plugs only. No CMC
  housings: their part numbers and mixed cavity layouts could not be confirmed.

Part numbers and wire ranges were checked against distributor listings (Nov 2026), which
sometimes disagree with each other (e.g. 0460-202-20141 is listed as 20 AWG, 18-20 AWG and
20-24 AWG). Every entry is marked `"approximate": true`. **Check the manufacturer's drawing
before ordering or crimping.** Wedgelocks (Deutsch W2P, W2S, ...) and other housing
accessories are not cavity parts and are not included. Cavity labels are plain numbers.
Deutsch DT seals are part of the housing grommet, so DT housings list no seals.
