# Example NTSC on the ch32v003 and ch32v006

Based on the NTSC guide [here](https://cnlohr.github.io/channel3/ntsc_pal_frame_documentation/pal_ntsc_from_kolumbus_fi_pami1.html)

In general:
 * Output NTSC on PD2
 * Use PD1 as SWIO
 * PD3 has debug timing

Default is to compile for 003, edit your makefile if you want 006.

If connecting to a TV (even a small one) please make sure all ground are connected **before** applying any power.

![Image](https://github.com/user-attachments/assets/616e7920-86bb-4726-be40-c3d0da39ef86)

