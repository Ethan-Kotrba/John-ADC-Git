import csv
import pandas as pd
import os


class Parse_Raw_Data(object):

    def __init__(self):
        self.Sample_Size = 7
        self.Time_Res = 3
        self.Raw_Data_Filename = "Raw_USB_Data.bin"
        self.Output_File = "ADC_DATA.csv"
        self.Script_Path = os.path.dirname(__file__)  # Gets the script's directory
        self.Raw_Data_Path = os.path.join(self.Script_Path, self.Raw_Data_Filename)
        self.Raw_Data = None

        self.VREF = 3.3
        self.Gain = 1


        self.Previous_Sample = None
        self.Previous_Timestamp = None
        self.Current_Sample = None
        self.Tol = 10000

    def Decode_Sample(self):
        self.Channel_ID = self.Current_Sample[0] >> 4  # Top 2 bits indicate channel
        
        # Extract 24-bit signed value (2's complement)
        #There might be an issue here since its assuming signed, but I thin,
        #you need the second have of the first byte for the sign
        raw_value = int.from_bytes(self.Current_Sample[1:4], byteorder='big', signed=False)
        # Convert to voltage: (raw_value / 2^23) * VREF / GAIN
        self.Voltage = (raw_value / (2**23)) * self.VREF / self.Gain
        #extract Timestamp
        self.Timestamp = int.from_bytes(self.Current_Sample[4:self.Sample_Size], byteorder='big')

    
    #Account for timestamp roll over
    def Is_Sample_Tricky(self, data, Tol):
        if self.Previous_Timestamp is None:
            return False
        timestamp = int.from_bytes(data[4:self.Sample_Size], byteorder='big')
        if (timestamp - self.Previous_Timestamp) & 0xFFFF < Tol:
            return False
        return True
        


    def Is_Sample_Valid(self, data):
        channel_id = (data[0] >> 4)
        sign = (data[0] & 0x0F)
        adc_data = int.from_bytes(data[1:4], byteorder='big', signed=False)
        if channel_id not in [0, 1]:
            return False
        if sign not in [0xF, 0x0]:
            return False
        if 0 > adc_data or adc_data > 0xFFFFFF:
            return False
        if self.Is_Sample_Tricky(data, 10000):
            return False
        return True
    
    def Realign_Data(self):
        print("Realigning")
        while True:
            data = self.Raw_Data.read(1)

            if len(data) != 1:
                continue

            #Check to see of channel id and sign nibble are valid
            if (data[0] >> 4) not in [0, 1] or (data[0] & 0x0F) not in [0x0, 0xF]:
                continue

            data_full = data + self.Raw_Data.read((self.Sample_Size-1))

            if self.Is_Sample_Valid(data_full):
                self.Current_Sample = data_full
                self.Decode_Sample()
                return
            

    def Open_File(self):
        self.Script_Path = os.path.dirname(__file__)  # Gets the script's directory
        self.Raw_Data_Path = os.path.join(self.Script_Path, self.Raw_Data_Filename)
        self.Raw_Data = open(self.Raw_Data_Path, 'rb')


    def Open_Output_File(self):
        self.Output = open(self.Output_File, 'w', newline='')
        self.CSV_Write = csv.writer(self.Output)
        self.CSV_Write.writerow(['Timestamp', 'Channel', 'Voltage'])

    def Write_Sample_To_CSV(self):
        self.CSV_Write.writerow([self.Timestamp, self.Channel_ID, f"{self.Voltage:.6f}"])
        self.Previous_Timestamp = self.Timestamp


    def Is_Channel_ID_Valid(self, Raw_Sample):
        channel_id = (Raw_Sample[0] >> 4)
        return channel_id not in [0, 1]
    

    def Is_Sample_Valid(self):
        #Check Channel_ID
        if self.Channel_ID not in [0, 1]:
            self.Realign_Data()



    def Get_Next_Valid_Sample(self):
        self.Current_Sample = self.Raw_Data.read(self.Sample_Size)
        self.Decode_Sample()
        if self.Is_Sample_Valid():
            return None


    
    def main(self):
        while(True):
            self.Get_Next_Valid_Sample()
            self.Write_Sample_To_CSV()
        self.Close_Files()


        


        








with open('/home/acid/Clones/pico/John-ADC-Git/PC_Side_Code/Collected_Data.bin', 'rb') as f:
    while True:
        sample = f.read(8)
        if not sample:
            break
        channel_id = sample[0] & 0x0F
        sign = sample[0] >> 4
        raw_value = int.from_bytes(sample[1:4], 'big')
        if sign == 0xF:
            raw_value -= 1 << 24
        voltage = (raw_value / (2 ** 23)) * 3.3 / 0.333
        timestamp = int.from_bytes(sample[4:8], 'little')
        print(f"Channel: {channel_id}, Voltage: {voltage:.6f} V, Timestamp: {timestamp}")