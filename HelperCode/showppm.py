import numpy as np
import serial
import time

port = '/dev/ttyUSB0'
speed = 9600

com = serial.Serial(port, baudrate=speed, timeout=1)

chararray = np.zeros(20, dtype=np.ubyte)
chararray.tostring()
q_ori = -7
r_ori = 2

def setbit(hexcoords):
    q,r = hexcoords
    q_arr = q - q_ori
    r_arr = r - r_ori + 6
    print(q_arr, r_arr)
    ch_byte = r_arr*2 
    if q_arr < 8:
        val=2**q_arr
        chararray[ch_byte+1]+=val
    else:
        val=2**(q_arr-8)
        chararray[ch_byte]+=val
		
ppmhexbits = [ (-7,2), (-7,3), (-7,4), (-7,5), (-6,1), (-6,3), (-5,1), (-5,2), # P
  (-3,0), (-3,1), (-3,2), (-3,3), (-2,-1), (-2,1), (-1,-1), (-1,0),          # P
  (1,-2), (1,-1), (1,0), (1,1), (2,-2), (3,-2), (4,-3), (5,-4), (5,-3), (5,-2), (5,-1) #M
 ]	

# clear hexarray
chararray = np.zeros(20, dtype=np.ubyte)

for coord in ppmhexbits:
    setbit(coord)
	
xstring_ppm = b'3000X'+chararray.tostring()

time.sleep(5)

while True:
    print(com.readlines())
    com.write(xstring_ppm)

