# usbpd-core-g0

Free and open usbpd stack, written for stm32 g0 UCPD.
No register access inside: hardware goes through usb_pd_platform.h

## Implemented
- Type-C: sink, source, DRP, DRP with Try.SNK
- PD 2.0 and 3.2, SOP only
- SPR PDOs: Fixed, Variable, Battery, PPS
- PR/DR/VCONN swap, FRS
- Get_Source_Cap, Get_Sink_Cap, Get_PPS_Status, Get_Sink_Cap_Extended, Get_Source_Info, Get_Revision
- Chunked extended messages
- Soft/hard resets
- BIST Carrier Mode and Test Data

## Not implemented
- EPR
- USB4
- VDM
- SOP'/SOP'' cable detection
- Unchunked extended messages
- Alert, Get_Status, battery messages, Get_Source_Cap_Extended, country info, Data_Reset
