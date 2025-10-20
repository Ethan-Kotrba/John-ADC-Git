import csv
import pandas as pd
import os


class Parse_Raw_Data(object):

    def __init__(self):
        self.Sample_Size = 8
        self.Time_Res = 3
        self.Raw_Data_Filename = "Raw_USB_Data.bin"
        self.Output_File = "ADC_DATA.csv"
        self.Script_Path = os.path.dirname(__file__)  # Gets the script's directory
        self.Raw_Data_Path = os.path.join(self.Script_Path, self.Raw_Data_Filename)
        self.Raw_Data = None
        self.Is_All_Data_Read = False

        self.Sample_Count = 0

        self.VREF = 3.3
        self.Gain = 1


        self.Previous_Sample = None
        self.Previous_Timestamp = None
        self.Current_Sample = None
        self.Tol = 10000

    def Decode_Sample(self):
        self.Channel_ID = (self.Current_Sample[0] >> 4)  # Top 2 bits indicate channel
        # Extract 24-bit signed value (2's complement)
        #There might be an issue here since its assuming signed, but I thin,
        #you need the second have of the first byte for the sign
        self.Raw_Value = int.from_bytes(self.Current_Sample[1:4], byteorder='big', signed=False)
        # Convert to voltage: (raw_value / 2^23) * VREF / GAIN
        self.Voltage = (self.Raw_Value / (2**23)) * self.VREF / self.Gain
        #extract Timestamp
        self.Timestamp = int.from_bytes(self.Current_Sample[4:self.Sample_Size], byteorder='big')

    
    # #Account for timestamp roll over
    # def Is_Sample_Tricky(self, data, Tol):
    #     if self.Previous_Timestamp is None:
    #         return False
    #     timestamp = int.from_bytes(data[4:self.Sample_Size], byteorder='big')
    #     if (timestamp - self.Previous_Timestamp) & 0xFFFF < Tol:
    #         return False
    #     return True
        


    # def Is_Sample_Valid(self, data):
    #     channel_id = (data[0] >> 4)
    #     sign = (data[0] & 0x0F)
    #     adc_data = int.from_bytes(data[1:4], byteorder='big', signed=False)
    #     if channel_id not in [0, 1]:
    #         return False
    #     if sign not in [0xF, 0x0]:
    #         return False
    #     if 0 > adc_data or adc_data > 0xFFFFFF:
    #         return False
    #     if self.Is_Sample_Tricky(data, 10000):
    #         return False
    #     return True
    
    def Realign_Data(self):
        print("Start Realigning")
        while True:
            data = self.Raw_Data.read(1)

            #Check To See If @ End of File
            if len(data) != 1:
                self.Is_All_Data_Read = True
                return None

            if not self.Is_Canidaite_Starting_Byte(data):
                print(f"Bad Starting Byte {data}")
                continue

            self.Current_Sample = data + self.Raw_Data.read((self.Sample_Size-1))
            print(f"About to check this sample {self.Current_Sample}")

            self.Decode_Sample()

            if self.Is_Sample_Valid():
                break
            

    def Open_File(self):
        self.Script_Path = os.path.dirname(__file__)  # Gets the script's directory
        self.Raw_Data_Path = os.path.join(self.Script_Path, self.Raw_Data_Filename)
        self.Raw_Data = open(self.Raw_Data_Path, 'rb')


    def Open_Output_File(self):
        self.Output = open(self.Output_File, 'w', newline='')
        self.CSV_Write = csv.writer(self.Output)
        self.CSV_Write.writerow(['Timestamp', 'Channel', 'Voltage'])

    def Write_Sample_To_CSV(self):
        print(f"========== Good enough to write {self.Current_Sample}")
        self.CSV_Write.writerow([self.Timestamp, self.Channel_ID, f"{self.Voltage:.6f}"])
        self.Previous_Timestamp = self.Timestamp

        if self.Sample_Count % 1000 == 0:
            self.Output.flush()


    # def Is_Channel_ID_Valid(self, Raw_Sample):
    #     channel_id = (Raw_Sample[0] >> 4)
    #     return channel_id not in [0, 1]
    
    def Is_Canidaite_Starting_Byte(self, data):
        channel_id = (data[0] >> 4)
        sign = (data[0] & 0x0F)
        if channel_id not in [0, 1]:
            print(f"Channel_ID not equal [0, 1] {channel_id}")
            return False
        print(sign)
        if sign not in [0xF, 0x0, 0, 15]:
            print(f"The Sign is off {sign}")
            return False
        print(f"This is a potential Good Bit {data}")
        return True 
    

    def Is_Sample_Valid(self):
        #Check Channel_ID
        if self.Channel_ID not in [0, 1]:
            print(f"Bad Channel Bit {self.Channel_ID}")
            print(self.Current_Sample)
            return False
        if 0 > self.Raw_Value or self.Raw_Value > 0xFFFFFF:
            print(f"Bad Raw_Value {self.Raw_Value}")
            print(self.Current_Sample)
            return False
        if self.Previous_Timestamp is None: #I hate this line
            print("No Previous_Timestamp")
            return True
        
        if (self.Timestamp - self.Previous_Timestamp) > self.Tol:
            print(f"Bad Timestamp {self.Timestamp} | {self.Previous_Timestamp}")
            print(self.Current_Sample)
            return False
        print(f"Passes Is Sample Valid {self.Current_Sample}")
        return True



    def Get_Next_Valid_Sample(self):
        self.Current_Sample = self.Raw_Data.read(self.Sample_Size)
        print(self.Current_Sample)
        if(len(self.Current_Sample) < 8):
            self.Is_All_Data_Read = True
            return
        self.Decode_Sample()
        if not self.Is_Sample_Valid():
            self.Realign_Data()

    


    def Setup(self):
        self.Open_File()
        self.Open_Output_File()
    
    def Parse_Data(self):
        while(True):
            self.Get_Next_Valid_Sample()
            if(self.Is_All_Data_Read):
                break
            self.Write_Sample_To_CSV()

    def Close_Files(self):
        self.Output.close()
        self.Raw_Data.close()

    def main(self):
        self.Setup()
        self.Parse_Data()
        self.Close_Files()


        
if __name__ == "__main__":
    Parser = Parse_Raw_Data()
    Parser.main()

        

