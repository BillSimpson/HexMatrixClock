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
		
fireworks = [ (-4,2), (-3,1), (-3,2), (1,-1), (1,0), (2,-1) ]

# wait five seconds for clock to reset
time.sleep(5)
print(com.readlines())

for i in range(5):
    # clear hexarray
    chararray = np.zeros(20, dtype=np.ubyte)
    for coord in fireworks:
        setbit(coord)
	
    xstring = b'4000X'+chararray.tostring()
    cstring = b'3000C'
    print(com.readlines())
    com.write(xstring)
    time.sleep(1)
    com.write(cstring)
    print(com.readlines())
    time.sleep(4)

