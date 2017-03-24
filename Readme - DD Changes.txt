MASTER files located in Arduino folder in Windows.

Increased homing velocity to 1050 mm/s to remove harmonic from homing cycle.

History.
DDv2.2 was 1000 (X/Y/Z)

DDv2.3 was 1500 (X) 2000 (Y/Z) mm/s.

DDv2.4 is 1050 (X/Y/Z) mm/s.  Slightly faster to remove a harmonic centered around 1000 mm/s.  Homing cycle incorrectly left at 1000 mm/s.

DDv2.5 is 1050 (X/Y/Z) mm/s, including during homing cycle.   

DDv2.6 is 1000 (X/Y/Z) mm/s, (homing stays at 1050 mm/s).  Junction deviation increases from 0.01 mm to 0.02 mm (to mirror 0.9g value)

DDv2.7 changed the probe code to a true interrupt.  Before the probe was polled during each stepper interrupt.
	2.6 and prior asked "is the probe tripped right now?"        every 1 ms.
	2.7 and later ask   "did the probe trip since I last asked?" every 1 ms.