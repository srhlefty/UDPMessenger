bool measureSDCardThroughput(unsigned int bytesToWrite, float& throughput)
{
	FIL file;
	FRESULT res;

	res = f_open(&file, "profile.dat", FA_CREATE_ALWAYS | FA_WRITE);
	if(res != FR_OK)
	{
		xil_printf("Unable to create file on SD card\r\n");
		return false;
	}

	res = f_lseek(&file, 0);
	if(res != FR_OK) return false;

	char* buf = new char[bytesToWrite];
	if(!buf) { xil_printf("Failed to allocate memory!\r\n"); return false; }

	u64 a = UDPMessenger::zynqGetTickCount();
	UINT bytesWritten;
	f_write(&file, buf, bytesToWrite, &bytesWritten);
	u64 b = UDPMessenger::zynqGetTickCount();

	f_close(&file);
	delete [] buf;

	if(bytesWritten != bytesToWrite)
	{
		xil_printf("Did not successfully write all bytes!\r\n");
		return false;
	}

	const float megaCountsPerSec = COUNTS_PER_SECOND / (1024*1024);
	throughput = bytesToWrite / ((b-a) / megaCountsPerSec);

	return true;
}
