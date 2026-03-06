# Enhanced Example Design

Replace the basic colored-squares example with a roguelike demo using the Angband 16x16 tileset.

## Map

- 80x50 tile dungeon, 3 layers (floor, walls/features, entities)
- Procedurally generated each run: 6-10 random non-overlapping rooms connected by L-shaped corridors
- Floor tiles, wall tiles, door tiles where corridors meet rooms
- Camera follows the player, scrolling when the map exceeds the 640x360 viewport (40x22 visible tiles)

## Player

- Angband player character tile (@)
- Tile-by-tile movement with WASD or arrow keys
- Collision with walls
- Starts in a random room

## Entities

Static, no AI:
- 5-10 monsters placed randomly in rooms (kobolds, orcs, snakes, etc.)
- 5-10 items scattered in rooms (gold piles, potions, scrolls)
- Walking over an item collects it (removes from map, increments score)
- Walking into a monster displays a message but blocks movement (no combat)

## HUD

- Bitmap font using ASCII glyphs from the Angband tileset
- Bar showing: player coordinates, item count, message line
- Message line updates contextually: "You see a kobold.", "You picked up gold.", "You bump into an orc."

## Tile Mapping

- Named constants for tile indices from the Angband 16x16 sheet layout
- Small curated set: ~5 terrain types, ~5 monster types, ~5 item types, player character, ASCII glyphs

## Assets

- Angband 16x16 tileset (BMP from Angband distribution, stored as PNG)
- Manifest updated with spritesheet and bitmap font entries
- Attribution note for the Angband tileset license

## Out of Scope

- Combat mechanics
- Monster AI / movement
- Inventory system
- Multiple dungeon levels
