# colors/ — Custom color palettes
#
# Create as many .txt files as you want in this folder.
# Each file defines a color palette selectable in:
#   Options > Visual > Color palette
#
# Format:
#   # Comment (lines starting with # are ignored)
#   name=Display name shown in-game
#   colors=rrggbb,rrggbb,rrggbb,...
#
# Rules:
#   - "name"   : optional (defaults to the filename without extension)
#   - "colors" : required — RGB hex codes (no #), comma-separated
#                1 to 64 colors; notes cycle through them in order
#
# Example:
#   name=My Palette
#   colors=ff5040,40c0ff,a060ff,ffcc30
