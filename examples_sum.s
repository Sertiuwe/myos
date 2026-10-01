; sum 1..100 на ассемблере EP32cm
; R0 = sum, R1 = counter
    MOVI R0, 0
    MOVI R1, 100
loop:
    CMPI R1, 0
    JLE  done
    ADD  R0, R0, R1
    SUBI R1, 1
    JMP  loop
done:
    OUT  0, R0       ; res0 = 5050
    HLT
