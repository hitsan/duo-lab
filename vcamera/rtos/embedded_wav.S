/* VCAMERA_WAV is the absolute WAV path, given with -D at build time */
    .section .rodata.embedded_wav, "a"
    .balign 4
    .global embedded_wav_start
embedded_wav_start:
    .incbin VCAMERA_WAV
    .global embedded_wav_end
embedded_wav_end:
