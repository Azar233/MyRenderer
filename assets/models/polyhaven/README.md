# Poly Haven CC0 showcase models

These five models were downloaded through the official Poly Haven API as the
1K glTF variants on 2026-09-15. The original files and textures are unmodified.
Poly Haven assets are released under CC0 1.0; attribution is not required for
the assets, but provenance is retained for reproducibility.

| Local directory | Asset | Author(s) | Source | Approx. triangles |
| --- | --- | --- | --- | ---: |
| `ArmChair_01` | Arm Chair 01 | Kirill Sannikov | <https://polyhaven.com/a/ArmChair_01> | 6K |
| `modern_coffee_table_01` | Modern Coffee Table 01 | Amin | <https://polyhaven.com/a/modern_coffee_table_01> | 5K |
| `ceramic_vase_01` | Ceramic Vase 01 | James Ray Cock | <https://polyhaven.com/a/ceramic_vase_01> | 3K |
| `anthurium_botany_01` | Anthurium Botany 01 | Rico Cilliers, Rob Tuytel | <https://polyhaven.com/a/anthurium_botany_01> | 4K |
| `bronze_whale_statue` | Bronze Whale Statue | Tina | <https://polyhaven.com/a/bronze_whale_statue> | 4K |

Every main glTF and dependency was verified against the MD5 value returned by
`https://api.polyhaven.com/files/{asset-id}`. The plant uses alpha-masked leaf
geometry and is intended for raster showcase/alpha validation; until CPU alpha
visibility is implemented it is excluded from strict Raster/Path Tracer scenes.
