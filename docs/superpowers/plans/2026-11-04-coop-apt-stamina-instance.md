# Stamina instance selection and private P2 preview

Continue checkpoint14 (`54a689d`) using reverse-engineer-anything. The current
Windows REA provider still excludes PPC; reuse generated native disassembly
and the owner's existing local asset/runtime evidence.

1. Mirror native transform-node push/pop in a bounded host-only ancestry stack:
   `8268C568` and alternate `8268C670` push, `82685290` pops. Validate balanced
   node identities before selecting packets; keep original calls unchanged.
2. Resolve node source/owning movie and exported character pointers from checked
   guest reads. Require Main_Hud -> StaminaMeter at native placement depth 172,
   then GenericMeter descendant identity. No recorded guest address constants.
3. Exercise runtime selection with distinct native stamina values and other
   GenericMeter widgets present. Prove ancestry, generation scoping, restoration
   and disabled behavior; record any gaps.
4. Separately derive fill-width/start-position relationships from actual asset
   methods and placements. If sufficient, implement an opt-in private packet/
   transform for the examined synchronous consumer only. Otherwise preserve
   the exact remaining geometric boundary before attempting pixels.
5. Tests/build/runtime, bounded review, checkpoint15 and milestone commit; no
   push and no generated/extracted assets in Git.

Completed: runtime ancestry in PID28836; private packet/transform preview and
same-capacity guards; raw asset formulas; visible A/B/C and leave/rejoin cases
in PID11040; 55 composed preview pairs, 2743 private packets, both generations;
diagnostic-off PID25380; 655 native tests and 182 tools tests/248 subtests.
Bounded review found no actionable correctness issues. Checkpoint15 records
the retained/shared effect and shield-state limits and the continuation work.
