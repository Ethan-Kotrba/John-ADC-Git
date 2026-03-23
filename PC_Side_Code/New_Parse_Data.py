import csv
import os



#This code was originally designed to be inside the read
#loop which is way the logic for parsing only looks a 1
#previous timestamp.
#It could be done better

class Parse_Raw_Data(object):

    def __init__(self, file_dir=None, filename=None):
        self.Sample_Size = 6
        self.Time_Res = 3
        self.Raw_Data_Filename = filename
        self.Raw_Data_Dir = file_dir
        self.Output_File = "Parsed" + filename[3:-4] + ".csv"
        
        self.Raw_Data_Path = os.path.join(file_dir, filename)
        self.Raw_Data = None
        self.Is_All_Data_Read = False

        self.Sample_Count = 0

        self.VREF = 3.3
        self.Gain = 1


        self.Previous_Sample = None
        self.Previous_Timestamp = None
        self.Previous_Good_Timestamp_Index = None
        self.Pervious_Good_Time = None
        self.Timestamp = None
        self.Current_Sample = None
        self.Tol = 1000
        self.Max_Time_Value = 65536

        self.Passed_Sample_Index = 0
        self.Bad_Read_Count = 0
        self.Total_Skipped_Bytes = 0













    def Open_File(self):
        self.Raw_Data_Path = os.path.join(self.Raw_Data_Dir, self.Raw_Data_Filename)
        self.Raw_Data = open(self.Raw_Data_Path, 'rb')

    def Open_Output_File(self):
        Outfile_Path = os.path.join(self.Raw_Data_Dir, self.Output_File)
        self.Output = open(Outfile_Path, 'w', newline='')
        self.CSV_Write = csv.writer(self.Output)
        self.CSV_Write.writerow(['Timestamp', 'Channel', 'Voltage'])

    def Setup(self):
        self.Open_File()
        self.Open_Output_File()






    def Decode_Sample(self):
        self.Channel_ID = (self.Current_Sample[0] >> 4)  # Top 2 bits indicate channel
        # Extract 24-bit signed value (2's complement)
        #There might be an issue here since its assuming signed, but I thin,
        #you need the second have of the first byte for the sign
        self.Raw_Value = int.from_bytes(self.Current_Sample[1:4], byteorder='big', signed=False)
        
        # Convert to voltage: (raw_value / 2^23) * VREF / GAIN
        self.Voltage = (self.Raw_Value / (2**23)) * self.VREF / self.Gain
        
        #extract Timestamp
        self.Previous_Timestamp = self.Timestamp
        self.Timestamp = int.from_bytes(self.Current_Sample[4:self.Sample_Size], byteorder='big')



    def Is_Canidaite_Starting_Byte(self, data):
        channel_id = (data[0] >> 4)
        sign = (data[0] & 0x0F)
        if channel_id not in [0, 1]:
            # print(f"Channel_ID not equal [0, 1] {channel_id}")
            return False
        # print(sign)
        if sign not in [0xF, 0x0, 0, 15]:
            # print(f"The Sign is off {sign}")
            return False
        # print(f"This is a potential Good Bit {data}")
        return True 

    def cSample_To_Str(self):
        """
        Converts a bytes object into a string of binary digits.

        Each byte is converted to its 8-bit binary representation.
        """
        # Use a list comprehension to format each byte (integer) as an 8-bit binary string
        # '{:08b}'.format(byte) ensures each binary number is padded with leading zeros
        # to always be 8 digits long.
        binary_strings = ['{:08b}'.format(byte) for byte in self.Current_Sample]
        
        # Join the list of 8-bit binary strings into a single continuous string
        self.Current_Sample_Str = ' '.join(binary_strings)

    def Print_Sample(self):
        self.cSample_To_Str()
        print(f"ID: {self.Channel_ID} Time: {self.Timestamp}")
        print(f"Sample Bin: {self.Current_Sample_Str}")

    def Is_Sample_Valid(self):
        #Check Channel_ID
        if self.Channel_ID not in [0, 1]:
            # print(f"Bad Channel Bit {self.Channel_ID}")
            # print(self.Current_Sample)
            print("Bad Sample: ID")
            # self.Print_Sample()
            return False
        if 0 > self.Raw_Value or self.Raw_Value > 0xFFFFFF:
            # print(f"Bad Raw_Value {self.Raw_Value}")
            # print(self.Current_Sample)
            print("Bad Sample: Value")
            # self.Print_Sample()
            return False
        if self.Previous_Timestamp is None: 
            return True
        if self.Previous_Good_Timestamp is None:
            return True
        
        Delta_T1 = ((self.Timestamp - self.Previous_Timestamp) % self.Max_Time_Value)
        Delta_T2 = ((self.Timestamp - self.Previous_Good_Timestamp) % self.Max_Time_Value)

        if(Delta_T1>self.Tol and Delta_T2>self.Tol):
            print("\nBad Sample: Timestamp")
            print(f"Sample_Index: {self.Passed_Sample_Index} | Bytes Skipped: {self.Skipped_Bytes}")
            print(f"Time, PTime, PGTime: {self.Timestamp} | {self.Previous_Timestamp} | {self.Previous_Good_Timestamp}")
            # print(f"Previous Timestamp: {self.Previous_Timestamp}")
            # print(f"Previous Goood Timestamp: {self.Previous_Good_Timestamp}")
            
            
            self.Print_Sample()
            # input("Press Enter To Continue: ")
            return False
        
        return True


    #This Function could be optimized.
    #I'm guestimating that 30% of our "by reads" throw out a good sample
    def Realign_Data(self):
        print("Start Realigning")
        self.Bad_Read_Count += 1
        self.Skipped_Bytes = -1
        while True:
            self.Skipped_Bytes += 1
            self.Total_Skipped_Bytes += 1
            data = self.Raw_Data.read(1)

            #Check To See If @ End of File
            if len(data) != 1:
                self.Is_All_Data_Read = True
                return None

            if not self.Is_Canidaite_Starting_Byte(data):
                # print(f"Bad Starting Byte {data}")
                continue

            self.Current_Sample = data + self.Raw_Data.read((self.Sample_Size-1))
            # print(f"About to check this sample {self.Current_Sample}")

            self.Decode_Sample()

            if self.Is_Sample_Valid():
                print(f"Skipped: {self.Skipped_Bytes} Bytes")
                break

            self.Skipped_Bytes += (self.Sample_Size-1)
            self.Total_Skipped_Bytes += (self.Sample_Size-1)


    def Get_Next_Valid_Sample(self):
        self.Previous_Good_Timestamp = self.Timestamp
        self.Current_Sample = self.Raw_Data.read(self.Sample_Size)
        # print(self.Current_Sample)
        if(len(self.Current_Sample) < self.Sample_Size):
            self.Is_All_Data_Read = True
            return
        self.Decode_Sample()
        if not self.Is_Sample_Valid():
            self.Realign_Data()

        # print(f"\nPassed: #{self.Passed_Sample_Index} Time: {self.Timestamp}")
        self.Passed_Sample_Index += 1





    def Write_Sample_To_CSV(self):
        # print(f"========== Good enough to write {self.Current_Sample}")
        self.CSV_Write.writerow([self.Timestamp, self.Channel_ID, f"{self.Voltage:.6f}"])
        self.Previous_Timestamp = self.Timestamp

        if self.Sample_Count % 1000 == 0:
            self.Output.flush()

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
        print("Parsing Completed ")
        print(f"\nPassed Samples: #{self.Passed_Sample_Index}\nBad Read Count {self.Bad_Read_Count}\nSkipped Bytes: {self.Total_Skipped_Bytes}")


def main():
    cwd = os.getcwd()
    Output_Dir = os.path.join(cwd, "PC_Side_Code", "Collected_Data")

    #======================ADD Filename here======================
    Filename = "Raw_Read_20260316_105907.bin"

    Parser = Parse_Raw_Data(file_dir=Output_Dir, filename=Filename)
    Parser.main()


    FilePath = r"/home/acid/Documents/Grad_School/Pico-ADC-Stuff/John-ADC-Git/PC_Side_Code/Collected_Data/Raw_Read_20260316_105907.bin"
    # Convert_Bin(FilePath)
    # Usage example:
    # Replace 'data.bin' with the path to your actual file
    


if __name__ == "__main__":
    main()
