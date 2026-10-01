; VMCU: записать 0x1234 в shared[0] (VMCU 0x8000 = ARM4 0x70000)
; потом прочитать обратно и вывести в res0
    MOVI R0, 32768       ; 0x8000
    MOVI R1, 0x1234
    STL  [R0+0], R1      ; shared[0] = 0x1234
    LDL  R2, [R0+0]      ; R2 = shared[0]
    OUT  0, R2           ; res0 = 0x1234
    HLT
