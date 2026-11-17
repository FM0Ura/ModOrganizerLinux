# small.mo2list

A tiny `.mo2list` for the tests (`Mo2ListZip.FixtureOpens`) and for trying the import by
hand. It is written by `Mo2ListZip.DISABLED_WriteFixture` in `test_mo2listzip.cpp`;
after changing that test, regenerate it with

```
organizer_tests --gtest_also_run_disabled_tests --gtest_filter=Mo2ListZip.DISABLED_WriteFixture
```

Game: Skyrim Special Edition (`skyrimspecialedition`). Content, from the lowest priority
to the highest:

| Mod | Source | Nexus mod id | Nexus file id |
| --- | --- | --- | --- |
| Interface | separator | | |
| SkyUI | nexus | 12604 | 35407 |
| Better MessageBox Controls | nexus | 1428 | 11023 |
| Better Dialogue Controls | nexus | 1429 | 11022 |
| Fixture Notes | bundled (`readme.txt`) | | |

Plus `profile/Skyrim.ini`, a README and six plugins (the five official masters and
`SkyUI_SE.esp`). No executables.

The ids were confirmed on 2026-11-17 with the public Nexus GraphQL API (`modFiles`):
the two Better Controls ids are their MAIN files (v1.2); SkyUI 35407 is the 5.2SE file,
now listed as an old version (still downloadable). MD5 and size are
left unknown (empty and -1), so the importer does not verify the archives.
