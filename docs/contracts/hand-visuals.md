# Contract — Hand visuals (Meta Interaction SDK)

Revised 2026-10-13. After the hand-built copy got placement right but not finger/button animation, the user chose to use the real SDK, copied from RayForge.

## Inputs
- Plugin `Plugins/MetaXRInteraction` (OculusInteraction v1.207.0, git-ignored, copied from RayForge). Worktrees need a junction to it.
- RayForge's working setup as the reference: `RayForge/Plugins/RayForge/Source/RayForgeActors/Private/ForgedVRPawn.cpp:80-196` (motion controllers, `UIsdkControllerRigComponentLeft/Right`, `IMC_IsdkControllerAnimation`).

## Behaviour
1. The SDK controller rigs (left/right) sit on their OWN motion controllers created at runtime under VROrigin, tagged `MemoryRoomVisualGrip`. The SDK switches those controllers' source to Left/RightAim itself; BP_VRPawn's LeftController/RightController are not touched.
2. Hands, fingers and controller-button animation come from the SDK (its ABPs + anim instances), fed by the SDK's own input mapping context, as RayForge does.
3. SDK interactors that would compete with MemoryRoom's own laser/selection/grab (ray, poke, grab interactors and their visuals) are disabled unless RayForge leaves them on and they don't interfere.
4. The left laser keeps starting from the left AIM pose (now: the SDK rig's left controller, or a plain LeftAim controller if that's cleaner).
5. Stock `UXRDeviceVisualizationComponent`s on the pawn stay hidden.

## Outputs
Hands + controllers look and animate like RayForge; MemoryRoom's laser, selection, grab, watch and wrist reveal behave as before.

## Errors
Plugin missing → build fails loudly (it's a hard dependency now). Rig fails to create → one warning, no hands.

## Perf budget
Quest 72 fps holds; no new per-frame allocations from MemoryRoom code.

## Removed
`UMemoryRoomHandVisualsComponent`, `UMemoryRoomHandAnimInstance`, `Content/MetaHands/**`, the `/OculusInteraction/ → /Game/MetaHands/` CoreRedirect, the runtime grip/trigger input mapping in HandVisuals.

## Done-check
Win64 editor + Win64 game + Android game targets build; in PIE hands/fingers/buttons animate like RayForge; laser/watch/grab unchanged; packaged Quest build still launches (checked before release, not per change).
