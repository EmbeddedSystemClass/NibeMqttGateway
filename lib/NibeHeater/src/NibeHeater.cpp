/**
*
*	This application listening data from Nibe F1145/F1245 heat pumps (RS485 bus)
*	and send valid frames to configurable IP/port address by UDP packets.
*	Application also acknowledge the valid packets to heat pump.
*
*	Serial settings: 9600 baud, 8 bits, Parity: none, Stop bits 1
*
*	MODBUS module support should be turned ON from the heat pump.
*
*	Frame format:
*	+----+----+----+-----+-----+----+----+-----+
*	| 5C | 00 | 20 | CMD | LEN |  DATA	 | CHK |
*	+----+----+----+-----+-----+----+----+-----+
*
*	Checksum: XOR
*
*	When valid data is received (checksum ok),
*	 ACK (0x06) should be sent to the heat pump.
*	When checksum mismatch,
*	 NAK (0x15) should be sent to the heat pump.
*
*	If heat pump does not receive acknowledge in certain time period,
*	pump will raise an alarm and alarm mode is activated.
*	Actions on alarm mode can be configured. The different alternatives
*	are that the Heat pump stops producing hot water (default setting)
*	and/or reduces the room temperature.
*
*
*/

#include "NibeHeater.h"
#include "DebugLog.h"

NibeHeater::NibeHeater()
{
	_rxMsgHandler = new NibeMessage(this, "Rx");
	_txMsgHandler = new NibeMessage(this, "Tx");
}

NibeHeater::NibeHeater(NibeMessage **ppMsg)
{
	_rxMsgHandler = *ppMsg = new NibeMessage(this, "Rx");
	_txMsgHandler = new NibeMessage(this, "Tx");
}

NibeHeater::NibeHeater(NibeMessage **ppMsg, IoContainer *pIoContainer)
{
	_ioContainer = pIoContainer;
	IoVal error = {0};
	error.i16Val = 0x5c5c;
	_ioContainer->SetErrorVal(eAnalog, error);	// Analog int16 messages with value 0x5c5c are wrong, seems to be an error status

	_rxMsgHandler = *ppMsg = new NibeMessage(this, "Rx");
	_txMsgHandler = new NibeMessage(this, "Tx");
}

void NibeHeater::AttachDebug(pDebugFunc debugfunc)
{
	_debugFunc = debugfunc;
}


void NibeHeater::SetReplyCallback(pFunc func)
{
	_rxMsgHandler->SetReplyCallback(func);
	_txMsgHandler->SetReplyCallback(func);

}

void NibeHeater::Loop()
{
	_rxMsgHandler->Loop();
}

//http://www.varmepumpsforum.com/vpforum/index.php?topic=39325.60

bool NibeHeater::HandleMessage(Message *pMsg)
{
	bool bOk = true;	// Return true of message is handled (set to false in default case)

	if (_debugFunc != nullptr)
	{
		//DEBUG_PRINT("%s", _msgHandler->LogMessage());
		//Serial.println(*_msgHandler);
		//_debugFunc(buff);//("Testdebug"); //->WriteHexString(pMsg->buffer, pMsg->msg.length + 5);
	}
	//Debug.println(*_rxMsgHandler);

	switch (pMsg->msg.command)
	{
	case DATABLOCK:
	case READDATA:
	{
		_rxMsgHandler->Send(ACK);

		int i = 0;
		const int datalength = 4;
		int size = 0;
		DataElement_t data = {0};
		do
		{
			#ifdef WIN32
			uint16_t adress = word(*(pMsg->msg.data + i), *(pMsg->msg.data + i + 1));
			#else
			uint16_t adress = word(*(pMsg->msg.data + i + 1), *(pMsg->msg.data + i));
			#endif

			if (size > 0 && adress != 0xffff)
			{
				_ioContainer->SetIoVal(data.adress, data.value.array, size);
				size = 0;
			}
			if (adress != 0xffff)
			{
				data.adress = adress;
			}
			if (size < sizeof(data.value.array) - 1)
			{
				data.value.array[size + 0] = pMsg->msg.data[i + 2];
				data.value.array[size + 1] = pMsg->msg.data[i + 3];
				size += 2;
			}
			i += datalength;
		} while (i < pMsg->msg.length);
	}
	break;
	case READREQ:
		if (ReadRequest(_ioContainer->GetExpiredIoElement(R), _txMsgHandler->GetMessage()))
		{
			DEBUG_PRINT("READREQ");
			_txMsgHandler->SendMessage();
		}
		else
		{
			_txMsgHandler->Send(ACK);
		}
	break;
	case WRITEREQ:
		if (WriteRequest(_ioContainer->GetExpiredIoElement(RW), _txMsgHandler->GetMessage()))
		{
			DEBUG_PRINT("WRITEREQ");
			_txMsgHandler->SendMessage();
		}
		else
		{
			_txMsgHandler->Send(ACK);
		}
		break;
	default:
		DEBUG_PRINT("Unknown message");
		_txMsgHandler->Send(ACK);	// This is an unknown message command, but we still send ack
		bOk = false;
	}

	return bOk;
}

bool NibeHeater::ReadRequest(int idx, Message *pMsg)
{
	bool bOk = false;
	IoElement *pIo = _ioContainer->GetIoElement(idx);

	if (pIo != nullptr && pMsg != nullptr)
	{
		DEBUG_PRINT("Reading %d-%d", idx,  pIo->nIdentifer);

		//C0 69 02 66 B8
		pMsg->msg.nodeid = 0xc0;
		pMsg->msg.command = READREQ;
		pMsg->msg.length = 2;//_ioContainer->GetIoSize(idx);
		#ifdef WIN32
		pMsg->msg.data[1] = pIo->nIdentifer & 0x00ff;
		pMsg->msg.data[0] = pIo->nIdentifer >> 8;
		#else
		pMsg->msg.data[0] = pIo->nIdentifer & 0x00ff;
		pMsg->msg.data[1] = pIo->nIdentifer >> 8;
		#endif
		bOk = true;
	}
	
	return bOk;
}

bool NibeHeater::WriteRequest(int idx, Message *pMsg)
{
	bool bOk = false;
	IoElement *pIo = _ioContainer->GetIoElement(idx);

	if (pIo != nullptr)
	{
		size_t dataSize = _ioContainer->GetIoSize(idx);
		
		// C0 6B 06 66 B8 CE FF 00 00 42
		pMsg->msg.nodeid = 0xc0;
		pMsg->msg.command = WRITEREQ;
		pMsg->msg.length = 2 + dataSize;	// Adress (2) + data
		#ifdef WIN32
		pMsg->msg.data[1] = pIo->nIdentifer & 0x00ff;
		pMsg->msg.data[0] = pIo->nIdentifer >> 8;
		#else
		pMsg->msg.data[0] = pIo->nIdentifer & 0x00ff;
		pMsg->msg.data[1] = pIo->nIdentifer >> 8;
		#endif		
		memcpy(&pMsg->msg.data[2], &pIo->ioVal, dataSize);

		bOk = true;
	}

	return bOk;
}

