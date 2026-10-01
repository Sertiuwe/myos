; VMCU читает из shared[4] (VMCU 0x8004 = ARM4 0x70004) и кладёт в res0
    MOVI R0, 32772       ; 0x8004
    LDL  R1, [R0+0]
    OUT  0, R1
    HLT
