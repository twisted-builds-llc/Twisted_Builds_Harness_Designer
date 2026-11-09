# `.tbh` file format

Current schema version: **1**. The extension and entry names are defined in
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
| `layout.json`     | Layout: `{ "nodes": [...], "segments": [...], "notes": [...] }`. |
| `schematic.json`  | Schematic placements (positions, collapse, visible rows).   |
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
  "schemaVersion": 1
}
```

`createdAt` is kept across saves. `modifiedAt` is set on every save.

## `harness.json`

```json
{
  "author": "",
  "description": "",
  "name": "Untitled harness",
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

- `units`: `in` | `mm` | `cm` (display unit only; default `in` when absent).
- `stripAllowanceMm` keys: `pin`, `splice`, `flyingLead`, `eyelet`, `ringTerminal`, `bare`.
  A missing key means 0 mm.

## `connectors.json`

```json
[
  {
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
        "sealPartNumber": "",
        "terminalPartNumber": ""
      }
    ]
  }
]
```

- `gender`: `none` | `male` | `female`.
- Pin order in the array is the display order. Pin `label` is free text.

## `wires.json`

```json
[
  {
    "baseColor": "RED",
    "conductorKind": "single",
    "endA": { "type": "pin", "connectorId": "c-ecu", "pinId": "ecu-p1" },
    "endB": { "type": "free", "kind": "ringTerminal", "label": "GND", "terminalPartNumber": "" },
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
  (`kind`: `flyingLead` | `eyelet` | `ringTerminal` | `bare`, plus `terminalPartNumber`, `label`).
- `gauge.system`: `awg` | `mm2`.
- `stripeColor`: `""` means no stripe.
- `lengthOverrideMm`: omitted when there is no manual override.
- `routeSegmentIds`: manual route override; empty means route automatically.
- `conductorKind`: only `single` in schema 1.

## `splices.json`

```json
[ { "id": "sp1", "label": "S1", "partNumber": "" } ]
```

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
  "notes": [
    { "id": "note1", "position": { "x": -40, "y": 120 }, "text": "Signal is blue, not yellow" }
  ]
}
```

- Node `type`: `connector` | `branch` | `splice` | `flyingLead` | `eyelet`. `refId` is the
  connector id (`connector`), splice id (`splice`), or a wire free-end key `<wireId>/a` or
  `<wireId>/b` (`flyingLead`, `eyelet`; eyelet is also used for ring terminals). `branch`
  nodes have no reference. Any node may have an empty `refId` (unassigned).
- At most one node may reference the same item.
- `position` is for drawing only. `lengthMm` is the entered branch length and is
  authoritative.
- `sleevingCoverage`: only `full` in schema 1.
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

## `schematic.json`

Presentation only: where items sit on the schematic canvas.

```json
{
  "placements": {
    "c-ecu":   { "collapsed": false, "maxVisibleRows": 40, "x": 0, "y": 0 },
    "sp1":     { "collapsed": false, "maxVisibleRows": 0, "x": 420, "y": 160 },
    "w3/b":    { "collapsed": false, "maxVisibleRows": 0, "x": 700, "y": 80 },
    "w4":      { "collapsed": false, "maxVisibleRows": 0, "x": 520, "y": 0 }
  }
}
```

- Keys are connector or splice ids, or `<wireId>/a` / `<wireId>/b` for a wire's free end.
- A key that is a wire id holds a dragged wire route: `x` is the scene x of the wire's
  vertical run (`y`, `collapsed` and `maxVisibleRows` are unused). Wires without one route
  their vertical run halfway between the ends. Older versions drop these entries on load,
  so the file stays readable; no `schemaVersion` change.
- `maxVisibleRows`: connector rows shown before the box scrolls; `0` shows all rows.
- `notes` (optional): free text on the schematic canvas, same shape as the layout notes:
  `[ { "id": "note2", "position": { "x": 500, "y": -80 }, "text": "Check ECU pinout" } ]`.
- Items without a placement are laid out automatically when the file is opened.
- Placements whose item no longer exists are dropped on load.

## `library.json`

The wire types and sleeving the harness references, copied from the user's parts library
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
