# `.tbh` file format

Current schema version: **2**. The extension and entry names are defined in
`src/core/io/FileFormat.h`.

A `.tbh` file is a zip container of UTF-8 JSON documents. JSON is written indented with object
keys sorted alphabetically, so the extracted documents diff cleanly in version control.

Saving is atomic: the file is written to a temporary file next to the target, then renamed.

## Entries

| Entry             | Content                                                     |
|-------------------|-------------------------------------------------------------|
| `manifest.json`   | Schema version and timestamps. Required.                    |
| `harness.json`    | Harness metadata and settings. Required.                    |
| `connectors.json` | Array of connectors (with pins). Optional, default `[]`.    |
| `wires.json`      | Array of wires. Optional, default `[]`.                     |
| `splices.json`    | Array of splices. Optional, default `[]`.                   |
| `freeEnds.json`   | Array of free ends (flying leads, eyelets...). Optional, default `[]`. |
| `layout.json`     | Layout: `{ "nodes": [...], "segments": [...], "notes": [...] }`. |
| `schematic.json`  | Schematic sheets and placements (positions, collapse, visible rows, sheet). |
| `library.json`    | Parts library entries this harness uses (embedded on save). |
| `bom.json`        | BOM rows added by hand (the rest of the BOM is generated).  |

Any other entry (for example from a newer version of the app) is kept byte-for-byte when
the file is re-saved.

## Conventions

- **Ids** are stable strings (UUIDs for new items). References always use ids.
- **Lengths** are millimeters, whatever the harness display unit. Keys end in `Mm`.
- **Optional strings** are written as `""` when empty.
- **Unknown keys** inside entity objects, `harness.json` and `settings` are preserved on save.
  Unknown keys inside wire ends, `gauge`, `position` and the top level of `layout.json` are not.
- Absent fields take their default. A field with the wrong JSON type is an error that names
  the location, e.g. `connectors[1].pins[0]: field "label" must be a string.`

## Validation on load

Loading fails with a message if:

- the zip, the manifest, or any JSON entry cannot be read;
- `schemaVersion` is missing, below 1, or newer than this build supports;
- ids are duplicated within a collection (pins: within their connector);
- a connector has fewer than 1 or more than 999 pins;
- a wire end, a wire `routeSegmentIds` entry, a layout node `refId` or a segment node
  reference points to something that does not exist, or a layout node `refId` does not
  match its type, or two layout nodes reference the same item;
- a segment connects a node to itself or has a negative length.

`wireTypeId` and `sleevingId` are library references and are not validated yet.

## `manifest.json`

```json
{
  "appVersion": "0.1.0",
  "createdAt": "2026-01-02T03:04:05.678Z",
  "modifiedAt": "2026-01-02T04:00:00.000Z",
  "schemaVersion": 3
}
```

`createdAt` is kept across saves. `modifiedAt` is set on every save.

## `harness.json`

```json
{
  "author": "",
  "description": "",
  "name": "Untitled harness",
  "partNumber": "",
  "revision": "",
  "settings": {
    "lengthRoundingIncrementMm": 1,
    "packingFactor": 0.7,
    "serviceLoopPercent": 0,
    "sleevingDiameterMarginPercent": 10,
    "stripAllowanceMm": { "pin": 6, "ringTerminal": 8.5 },
    "wasteFactorSleevingPercent": 5,
    "wasteFactorWirePercent": 5
  },
  "units": "in"
}
```

- `partNumber`: the harness part number, printed in the PDF title block. Added after schema 2
  without a version bump: it is optional (absent = `""`), and older builds keep it on save
  because unknown `harness.json` keys are preserved.
- `units`: `in` | `mm` | `cm` (display unit only; default `in` when absent).
- `stripAllowanceMm` keys: `pin`, `splice`, `flyingLead`, `eyelet`, `ringTerminal`, `bare`.
  A missing key means 0 mm.

## `connectors.json`

```json
[
  {
    "backshellPartNumber": "",
    "description": "",
    "gender": "female",
    "id": "c-ecu",
    "label": "ECU-A",
    "manufacturer": "",
    "notes": "",
    "partNumber": "",
    "pins": [
      {
        "circuitText": "5V REF",
        "id": "ecu-p1",
        "isCavityPlug": false,
        "label": "A1",
        "notes": "",
        "plugPartNumber": "",
        "sealPartNumber": "",
        "terminalPartNumber": ""
      }
    ]
  }
]
```

- `gender`: `none` | `male` | `female`.
- `factory` (schema 3, optional, default `false`, written only when `true`): the connector is
  part of an existing (factory) harness: drawn dashed, not counted in the BOM except the
  terminals and seals of pins with non-factory wires, and left off pinouts unless such pins
  exist.
- `backshellPartNumber`, `strainReliefPartNumber` (schema 3, may be empty): the connector's
  backshell and the strain relief fitted at it (e.g. a zip tie or clamp on the backshell),
  counted in the BOM (strain relief with the layout ties).
- Pin order in the array is the display order. Pin `label` is free text.
- `isCavityPlug`: the cavity is fitted with a blind plug; `plugPartNumber` is that plug
  (schema 3). The BOM counts plugs from `plugPartNumber` and, if set, a seal from
  `sealPartNumber`.

## `wires.json`

```json
[
  {
    "baseColor": "RED",
    "circuitId": "C001",
    "conductorKind": "single",
    "endA": { "type": "pin", "connectorId": "c-ecu", "pinId": "ecu-p1" },
    "endB": { "type": "free", "freeEndId": "fe1" },
    "gauge": { "system": "awg", "value": 20 },
    "id": "w1",
    "lengthOverrideMm": 1234.5,
    "name": "5V REF",
    "routeSegmentIds": [],
    "stripeColor": "",
    "wireTypeId": ""
  }
]
```

- Wire end `type`: `pin` (`connectorId`, `pinId`), `splice` (`spliceId`) or `free`
  (`freeEndId`, an entry of `freeEnds.json`).
- `gauge.system`: `awg` | `mm2`.
- `stripeColor`: `""` means no stripe.
- `lengthOverrideMm`: omitted when there is no manual override.
- `routeSegmentIds`: manual route override; empty means route automatically.
- `circuitId` (schema 3): the circuit number printed on cut lists and pinouts. New wires
  get `C` + the next number (three digits or more); it is free text and may be edited.
  Duplicates load fine and are reported in the Problems panel.
- `conductorKind`: only `single` in schema 1.
- `factory` (schema 3, optional, default `false`, written only when `true`): the wire is part
  of an existing (factory) harness: drawn dashed, left out of the BOM, cut list and pinouts.
- `leadA` / `leadB` (schema 3, optional, default `auto`, written only when not `auto`):
  `left` | `right`, the lead of the splice or free end at that end the wire leaves from on
  the schematic (the item's own left/right). `auto`: a splice wire meets the dot, a free end
  wire the lead facing the wire's other end. Ignored on pin ends.

## `splices.json`

```json
[ { "id": "sp1", "label": "S1", "partNumber": "" } ]
```

- `symbol` (schema 3, optional, default `none`, written only when set): `ground` |
  `chassisGround` | `power`, drawn on the schematic only so a ground or supply point stands
  out. The splice itself (layout, BOM) does not change. Unknown values are refused.

## `freeEnds.json`

A wire end that is not on a connector or splice. Placed on the schematic on its own; one or
more wires end on it.

```json
[ { "id": "fe1", "kind": "ringTerminal", "label": "GND", "terminalPartNumber": "RING-M6" } ]
```

- `kind`: `flyingLead` | `eyelet` | `ringTerminal` | `bare` | `cut` (schema 3: where a wire
  was cut, e.g. a factory wire cut to splice in an add-on; never counted in the BOM).
- `symbol` (schema 3, optional): as on splices. With `power` the `label` names the supply,
  e.g. `"12V+"`.
- `pairedCutId` (schema 3, optional, `cut` ends only, written only when set): id of the other
  cut end of the same cut (both ends share one layout point). Must name an existing free end.
  Absent for cuts made before v0.1.1's pairing; such cut ends are joined by hand.

## `layout.json`

```json
{
  "nodes": [
    { "id": "n1", "position": { "x": 0, "y": 0 }, "refId": "c-ecu", "type": "connector" }
  ],
  "segments": [
    {
      "id": "seg1", "label": "", "lengthMm": 250, "nodeA": "n1", "nodeB": "n2",
      "sleevingCoverage": "full", "sleevingId": "", "bend": { "x": 0.5, "y": 0.25 }
    }
  ],
  "accessories": [
    {
      "coverLengthMm": 38.1, "coverSleevingId": "seed-hs2-3-8", "id": "acc1", "kind": "label",
      "notes": "", "offsetMm": 50.8, "partNumber": "LBL-1", "segmentId": "seg1", "text": "ECU-A"
    }
  ],
  "notes": [
    { "id": "note1", "position": { "x": -40, "y": 120 }, "text": "Signal is blue, not yellow" }
  ]
}
```

- Node `type`: `connector` | `branch` | `splice` | `flyingLead` | `eyelet`. `refId` is the
  connector id (`connector`), splice id (`splice`), or free end id (`flyingLead`, `eyelet`;
  eyelet is also used for ring terminals). `branch`
  nodes have no reference. Any node may have an empty `refId` (unassigned).
- `sharedRefIds` (optional, `splice`, `flyingLead` and `eyelet` nodes only): further splice
  or free end ids at the same point in the loom as the node's own `refId`: splices side by
  side (the same distance from a connector), or the two cut ends of a cut wire. Written only
  when non-empty. The node must have a non-empty `refId`. Files written before v0.1.1 name
  this list `sharedSpliceIds` (splice ids only); it is still read.
- At most one node may reference the same item (through `refId` or `sharedSpliceIds`).
- `position` is for drawing only. `lengthMm` is the entered branch length and is
  authoritative.
- `sleevingCoverage`: only `full` in schema 1.
- `addOnSleeving` (optional, default `false`, written only when `true`): on a segment that
  carries only factory wires, the sleeving is added by this build (e.g. over a cut and splice
  into the factory harness) and counts in the BOM. Without it such a segment's sleeving is
  taken to be the factory harness's own and left out. Older versions keep the key unread.
- `bend` (optional, drawing only): the segment is drawn as a quadratic Bezier curve whose
  control point is `A + x * (B - A) + y * n`, where A and B are the positions of `nodeA`
  and `nodeB` and `n = (-(B - A).y, (B - A).x)` (B - A turned 90 degrees). Being relative
  to the ends, the curve follows when the nodes move. Absent = straight. `{ "x": 0.5,
  "y": 0 }` is also straight. It does not affect `lengthMm`.
- `notes` (optional): free text on the layout canvas: `{ "id", "text", "position" }`.
  `position` is the top left of the text; drawing only. Note ids are unique across
  `layout.json` and `schematic.json`.
- `bend` and `notes` (here and in `schematic.json`) were added without a `schemaVersion`
  change: files without them load as before.
- `accessories` (schema 3): things fitted along the loom and counted in the BOM. `kind`:
  `label` (an identification label; `text` is what is printed on it, `coverSleevingId` +
  `coverLengthMm` an optional protective cover such as clear heat shrink, counted in the
  sleeving totals) or `tie` (zip tie / strain relief; no cover). Each sits on `segmentId`,
  `offsetMm` from the segment's `nodeA` along its entered length. `partNumber`, `notes` free
  text. The segment must exist.

## `schematic.json`

Presentation only: where items sit on the schematic canvas.

```json
{
  "placements": {
    "c-ecu":   { "collapsed": false, "maxVisibleRows": 40, "x": 0, "y": 0 },
    "sp1":     { "collapsed": false, "maxVisibleRows": 0, "x": 420, "y": 160 },
    "fe1":     { "collapsed": false, "maxVisibleRows": 0, "x": 700, "y": 80 },
    "w4":      { "collapsed": false, "maxVisibleRows": 0, "x": 520, "y": 0 }
  }
}
```

- Keys are connector, splice or free end ids.
- A key that is a wire id holds a dragged wire route: `x` is the scene x of the wire's
  vertical run (`y`, `collapsed` and `maxVisibleRows` are unused). Wires without one route
  their vertical run halfway between the ends. Older versions drop these entries on load,
  so the file stays readable; no `schemaVersion` change.
- `maxVisibleRows`: connector rows shown before the box scrolls; `0` shows all rows.
- `sheets` (optional): the schematic's sheets (pages), in order:
  `[ { "id": "sheet1", "name": "Engine" }, { "id": "k3f9", "name": "" } ]`. An empty
  `name` displays as "Sheet N" by position. Missing or empty means one sheet with id
  `sheet1`. Sheet ids must be unique. Each sheet has its own canvas coordinates.
- `sheet` (optional, on connector, splice and free end placements and on schematic notes):
  the id of the sheet the item is on. Missing, empty or an id that names no sheet means the
  first sheet (unknown ids are cleared on load). Wire route placements have none: a wire is
  drawn on the sheets of its ends, and as a lead to an off-sheet tag where they differ.
- `alsoOn` (optional, on connector, splice and free end placements): further sheets the
  same item is shown on, with its position on each: `{ "k3f9": { "x": 0, "y": 40 } }`. The
  item stays one item (one connector or splice in the layout and BOM); `x`/`y`/`sheet` are
  its home sheet. Entries naming no sheet, or the home sheet, are dropped on load.
- A key `<wire id>#sheet` holds the sheets a wire is shown on: `sheet` (the one it was
  drawn on) plus `alsoOn` keys for further sheets (their `x`/`y` are unused). It stays one
  wire. It is drawn in full on each of those sheets that shows both its ends; without any,
  on the first sheet (in order) showing both; else as a lead to an off-sheet tag at each
  end. Dropped on load when the wire or its `sheet` is gone.
- A dragged wire route (key = wire id) is kept per sheet the same way: `x` on its `sheet`,
  `alsoOn` for the others.
- `sheets`, `sheet`, `alsoOn` and `#sheet` keys were added without a `schemaVersion` change: older files load as one
  sheet, and older versions keep the keys but draw every sheet on one canvas.
- `notes` (optional): free text on the schematic canvas, same shape as the layout notes:
  `[ { "id": "note2", "position": { "x": 500, "y": -80 }, "text": "Check ECU pinout" } ]`.
- `frame` (schema 3, optional, schematic notes only): `{ "width": 320, "height": 200 }`
  makes the note an outline, e.g. around the connectors of one assembly: a dashed box of
  that size from `position`, with `text` as its title (may be empty). Both sizes must be
  greater than zero. Older versions keep the key and draw the title as a plain note.
- Items without a placement are laid out automatically when the file is opened.
- Placements whose item no longer exists are dropped on load.

## `library.json`

The wire types, sleeving and connector parts the harness references, copied from the user's parts library
on every save so the file opens the same on another machine. The same format is used for
the per-user library file (`library.json` in the app data folder).

```json
{
  "sleevings": [
    {
      "approximate": true, "family": "PET expandable braid", "id": "seed-pet-1-4",
      "innerDiameterMm": 6.4, "kind": "braided", "sizeLabel": "1/4 in"
    },
    {
      "approximate": true, "family": "Heat shrink 2:1", "id": "seed-hs2-1-4",
      "innerDiameterMm": 6.4, "kind": "heatShrink", "recoveredDiameterMm": 3.2, "sizeLabel": "1/4 in"
    }
  ],
  "wireTypes": [
    {
      "approximate": true, "id": "seed-gxl", "name": "GXL",
      "outerDiameters": [ { "gauge": { "system": "awg", "value": 18 }, "odMm": 2.06 } ]
    }
  ]
}
```

- Wires reference a wire type by `wireTypeId`; segments reference sleeving by `sleevingId`.
- `kind`: `braided` | `splitLoom` | `heatShrink` | `other`. `recoveredDiameterMm` is only
  meaningful for heat shrink and is omitted otherwise.
- `approximate: true` marks shipped seed values the user has not verified; the app labels
  them as approximate.
- When opening, the user's own library wins for ids present in both; embedded entries fill
  in ids the user's library lacks.

### Connector parts (`connectorParts`, `pinParts`; v0.1.1)

```json
{
  "connectorParts": [
    {
      "approximate": true, "cavityLabels": ["1", "2"], "description": "2-way receptacle",
      "family": "Deutsch DT", "gender": "male", "manufacturer": "Deutsch",
      "partNumber": "DT04-2P", "plugs": ["114017"], "seals": [], "terminals": ["0460-202-16141"]
    }
  ],
  "pinParts": [
    {
      "approximate": true, "description": "Size 16 pin", "insulationMaxMm": 3.0,
      "insulationMinMm": 1.35, "kind": "terminal", "manufacturer": "Deutsch",
      "partNumber": "0460-202-16141",
      "wireMax": { "system": "awg", "value": 14 }, "wireMin": { "system": "awg", "value": 16 }
    }
  ]
}
```

(Example values for the format only, not verified part data.)

- Connector parts are identified by `partNumber`, unique per list; pin parts by `kind`
  (`terminal` | `seal` | `plug`) plus `partNumber`. Connectors and pins refer to them by the
  part numbers they store (`partNumber`, `terminalPartNumber`, `sealPartNumber`,
  `plugPartNumber`, free end `terminalPartNumber`); those are what is embedded on save.
- `cavityLabels`: one per cavity, in order. `terminals` / `seals` / `plugs`: part numbers made
  for this housing.
- `wireMin` / `wireMax`: conductor sizes the terminal takes (AWG or mm2, compared by
  cross-section; either order). `insulationMinMm` / `insulationMaxMm`: insulation outer
  diameter range. Each is omitted when not specified.
- Older readers ignore these keys; files using them are schema 3.

## `bom.json`

Only the rows the user adds by hand are stored; wire, sleeving and part lines are generated
from the model every time.

```json
{
  "extras": [
    { "description": "Cloth tape, 19 mm", "id": "x1", "notes": "", "partNumber": "TAPE-19",
      "quantity": 2.5, "unit": "roll" }
  ]
}
```

## Schema changes

Bump `kSchemaVersion` and add a `migrate_vN_to_vN+1` step to `src/core/io/Migrations.cpp`
for every change older readers cannot handle. Files with a newer schema than the build
supports are refused, never silently downgraded.

### History

- **2**: free ends became entities in `freeEnds.json`. In schema 1 a wire end held the free
  end itself (`{ "type": "free", "kind", "label", "terminalPartNumber" }`), and its schematic
  placement and layout node used the key `<wireId>/a` or `<wireId>/b`. The 1 to 2 migration
  gives each such end an id, moves it to `freeEnds.json`, and rewrites the wire end, the
  placement key and the layout `refId`.
- **3** (v0.1.1): pins gained `plugPartNumber`. Schema 2 files kept a cavity plug's part
  number in `sealPartNumber`; the 2 to 3 migration moves it to `plugPartNumber` on pins with
  `isCavityPlug` and clears the seal. Wires gained `circuitId`; the migration numbers
  wires without one C001, C002, ... in file order. Other v0.1.1 schema additions also go in
  schema 3: `factory` on connectors and wires, the `cut` free end kind, wire
  `leadA`/`leadB`, splice and free end `symbol`, cut end `pairedCutId` and schematic note `frame` (no migration;
  absent means not factory, `auto` leads, no symbol and a plain note).

## Parts CSV import (Parts Library > Import Parts CSV...)

Not part of `.tbh` files: a spreadsheet format for adding connector parts to the user's
library (`src/core/io/PartsCsv.h`). One row per part; the first line names the columns, in
any order (case and anything in parentheses ignored; unknown columns ignored). Comma or
semicolon separated, quotes allowed, UTF-8. *Save CSV Template...* writes an example.

| Column | Used for | Value |
|---|---|---|
| Type | all (required) | `housing`, `terminal`, `seal` or `plug` |
| Part number | all (required) | replaces a library entry with the same number (and kind) |
| Manufacturer, Description | all | text |
| Family, Contacts | housings | text; `male`/`pins`, `female`/`sockets` or `none` |
| Cavities | housings (required) | label pattern: `1-12`, `A1-A8, B1-B8` |
| Terminals, Seals, Plugs | housings | part numbers separated by `;` or `,` |
| Wire from, Wire to | terminals | `20 AWG`, `0.5 mm2`; no unit = AWG |
| Insulation min, Insulation max | terminals, seals | outer diameter in mm |

Rows with a problem are skipped and listed by line number. Imported parts are not marked
approximate.
