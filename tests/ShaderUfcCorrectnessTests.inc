// Regression cases ported from Dev 124 onto the upstream test harness.
// Keep lane-distinct GPU inputs so readfirstlane cannot fold to a host constant.
std::vector<TestCase> ReadFirstLaneExecCases() {
  using O = ShaderOpcode;
  std::vector<TestCase> cases;
  static std::string names[2][4];
  for (u32 wave : {32u, 64u}) {
    for (u32 mask_case = 0; mask_case < 4; ++mask_case) {
      const u32 low = mask_case == 1 ? 0x30u : mask_case == 3 ? 0x80u : 0u;
      const u32 high = mask_case == 2 ? 0x300u : mask_case == 3 ? 0x2u : 0u;
      const u32 selected = mask_case == 1 ? 4u : mask_case == 3 ? 7u :
                           mask_case == 2 && wave == 64 ? 40u : 0u;
      auto &name = names[wave == 64][mask_case];
      name = "ReadFirstLaneExecWave" + std::to_string(wave) + "Mask" +
             std::to_string(mask_case);
      TestCase test;
      test.name = name.c_str();
      test.initial.resize(128 + wave);
      for (u32 lane = 0; lane < wave; ++lane) {
        test.initial[128 + lane] = 0x12340000u + lane * 0x101u;
      }
      auto &code = test.code;
      code.push_back(EncodeVop2(0x1a, 30, InlineU32(2), 0));
      AppendVMovU32(&code, 2, 512);
      code.push_back(EncodeVop2(0x25, 30, Vgpr(2), 30));
      AppendBufferLoadDword(&code, 1, 30);
      AppendSMovLiteral(&code, 126, low);
      AppendSMovLiteral(&code, 127, high);
      code.push_back(EncodeVop1(0x02, 20, Vgpr(1)));
      AppendSMovLiteral(&code, 126, 0xffffffffu);
      AppendSMovLiteral(&code, 127, 0xffffffffu);
      AppendStoreSgprAtLaneDwordOffset(&code, 20, 0, 0);
      AppendEnd(&code);
      test.expected = test.initial;
      std::fill_n(test.expected.begin(), wave, test.initial[128 + selected]);
      test.opcodes = {O::V_LSHLREV_B32, O::V_MOV_B32, O::V_ADD_NC_U32,
                      O::BUFFER_LOAD_DWORD, O::S_MOV_B32, O::V_READFIRSTLANE_B32,
                      O::BUFFER_STORE_DWORD, O::S_ENDPGM};
      test.required_spirv = {"OpGroupNonUniformShuffle", "OpGroupNonUniformBallot"};
      test.compute_info.wave_size = wave;
      test.compute_info.threads_num[0] = wave;
      test.compute_info.threads_num[1] = 1;
      test.compute_info.threads_num[2] = 1;
      test.compute_info.thread_ids_num = 1;
      test.has_compute_info = true;
      cases.push_back(std::move(test));
    }
  }
  return cases;
}

TestCase VectorDpp16InactiveSource(u32 wave_size, u32 control, bool add,
                                 bool fetch_inactive, bool bound_control) {
  using O = ShaderOpcode;
  constexpr u32 sentinel = 0xdeaddeadu;
  constexpr u32 masks[] = {0x33cc0ff3u, 0xc3a5960fu};
  const u32 row_mask = control == 0x140u ? 0x5u : 0xfu;
  const u32 bank_mask = control == 0x140u ? 0xau : 0xfu;
  TestCase test;
  static std::string names[2][3][2][2][2];
  auto &name = names[wave_size == 64][control == 0x4bu ? 0 : control == 0x111u ? 1 : 2]
                    [add][fetch_inactive][bound_control];
  if (name.empty()) {
    name = "VectorDpp16InactiveSourceWave" + std::to_string(wave_size) +
           "Ctrl" + std::to_string(control) + (add ? "Add" : "Mov") +
           (fetch_inactive ? "Fi1" : "Fi0") + (bound_control ? "Bc1" : "Bc0");
  }
  test.name = name.c_str();
  auto &code = test.code;
  AppendVMovU32(&code, 5, 100);
  code.push_back(EncodeVop2(0x25, 5, Vgpr(0), 5));
  AppendVMovU32(&code, 1, 1000);
  code.push_back(EncodeVop2(0x25, 1, Vgpr(0), 1));
  AppendVMovLiteral(&code, 2, sentinel);
  AppendSMovLiteral(&code, 126, masks[0]);
  AppendSMovLiteral(&code, 127, masks[1]);
  code.push_back(add ? EncodeVop2(0x25, 2, 250, 1) : EncodeVop1(0x01, 2, 250));
  code.push_back(EncodeVop1Dpp(5, control, row_mask, bank_mask) |
                 (u32(fetch_inactive) << 18u) | (u32(bound_control) << 19u));
  AppendSMovLiteral(&code, 126, 0xffffffffu);
  AppendSMovLiteral(&code, 127, 0xffffffffu);
  AppendStoreVgprAtLaneDwordOffset(&code, 2, 0, 0);
  AppendEnd(&code);
  test.expected.resize(wave_size, sentinel);
  const auto active = [&](u32 lane) {
    return lane < wave_size && (masks[lane / 32u] & (1u << (lane % 32u))) != 0;
  };
  for (u32 lane = 0; lane < wave_size; ++lane) {
    const bool masks_ok = (row_mask & (1u << (lane / 16u))) != 0 &&
                          (bank_mask & (1u << ((lane / 4u) % 4u))) != 0;
    const bool valid = control != 0x111u || (lane % 16u) != 0;
    const u32 source = control == 0x4bu
                           ? (lane & ~3u) | ((control >> ((lane % 4u) * 2u)) & 3u)
                       : control == 0x140u ? lane ^ 15u : lane - 1u;
    if (!active(lane) || !masks_ok ||
        (!bound_control && (!valid || (!fetch_inactive && !active(source))))) {
      continue;
    }
    const u32 fetched = valid && (fetch_inactive || active(source)) ? 100u + source : 0u;
    test.expected[lane] = fetched + (add ? 1000u + lane : 0u);
  }
  test.opcodes = {O::V_MOV_B32, O::V_ADD_NC_U32, O::S_MOV_B32,
                  O::BUFFER_STORE_DWORD, O::S_ENDPGM};
  test.required_spirv = {"OpGroupNonUniformShuffle"};
  test.compute_info.wave_size = wave_size;
  test.compute_info.threads_num[0] = wave_size;
  test.compute_info.threads_num[1] = 1;
  test.compute_info.threads_num[2] = 1;
  test.compute_info.thread_ids_num = 1;
  test.has_compute_info = true;
  return test;
}

std::vector<TestCase> Dpp16InactiveSourceCases() {
  std::vector<TestCase> cases;
  for (u32 wave_size : {32u, 64u}) {
    for (u32 control : {0x4bu, 0x111u, 0x140u}) {
      for (bool add : {false, true}) {
        for (bool fi : {false, true}) {
          // FI plus out-of-bounds fetch is a separate ISA contract.
          if (control == 0x111u && fi) continue;
          for (bool bc : {false, true}) {
            cases.push_back(VectorDpp16InactiveSource(wave_size, control, add, fi, bc));
          }
        }
      }
    }
  }
  return cases;
}
