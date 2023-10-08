package main

import (
	"fmt"
	"github.com/go-audio/audio"
	"github.com/go-audio/wav"

	"github.com/google/gopacket"
	"github.com/google/gopacket/layers"
	"github.com/google/gopacket/pcap"
	"log"
	"os"
)

func writeWAV(filename string, data *audio.IntBuffer) error {
	// Create the WAV file
	file, err := os.Create(filename)
	if err != nil {
		return err
	}
	defer file.Close()
	// Set up the WAV file header
	encoder := wav.NewEncoder(file, 48000, 16, 2, 1)
	// Write the WAV file header
	err = encoder.Write(data)
	if err != nil {
		return err
	}
	_ = encoder.Close()
	// Convert the float32 data to int32
	return nil
}

func main() {
	if len(os.Args) != 3 {
		fmt.Println("Usage: go run main.go <input.pcap> <output.bin>")
		return
	}

	// Open the PCAP file
	handle, err := pcap.OpenOffline(os.Args[1])
	if err != nil {
		log.Fatal(err)
	}
	defer handle.Close()

	var iqdata []int
	logg := ""
	txtfile, err := os.Create(os.Args[2] + ".txt")
	if err != nil {
		log.Fatal(err)
	}
	defer txtfile.Close()

	packetSource := gopacket.NewPacketSource(handle, handle.LinkType())
	for packet := range packetSource.Packets() {
		udpLayer := packet.Layer(layers.LayerTypeUDP)
		if udpLayer != nil {
			udpPacket, _ := udpLayer.(*layers.UDP)
			payload := udpPacket.Payload

			// Check if the packet is IPv4 to avoid writing corrupted data
			ip4Layer := packet.Layer(layers.LayerTypeIPv4)
			if ip4Layer != nil {
				ip4Packet, _ := ip4Layer.(*layers.IPv4)
				if ip4Packet.Version == 4 {
					offset := 8 // pkt header
					offset += 8 // sub header
					for q := 0; q < 63; q++ {
						ival := float64(int8(payload[offset+5]-128)) + float64(int8(payload[offset+4]-128))*256
						qval := float64(int8(payload[offset+7]-128)) + float64(int8(payload[offset+6]-128))*256
						iqdata = append(iqdata, int(ival))
						iqdata = append(iqdata, int(qval))
						logg += fmt.Sprintf("%f,%f\n", ival, qval)
						offset += 8
					}
					offset += 8
					for q := 0; q < 63; q++ {
						ival := float64(int8(payload[offset+5]-128)) + float64(int8(payload[offset+4]-128))*256
						qval := float64(int8(payload[offset+7]-128)) + float64(int8(payload[offset+6]-128))*256
						iqdata = append(iqdata, int(ival))
						iqdata = append(iqdata, int(qval))
						logg += fmt.Sprintf("%f,%f\n", ival, qval)
						offset += 8
					}
				}
			}
		}
		txtfile.WriteString(logg)
		logg = ""
	}
	buf := &audio.IntBuffer{&audio.Format{
		NumChannels: 2,
		SampleRate:  48000,
	}, iqdata, 16}

	// Write the UDP payloads to a binary file

	// Write the IQ data to a stereo wav file
	err = writeWAV(os.Args[2], buf)
	if err != nil {
		log.Fatal(err)
	}

	fmt.Println("Extraction complete.")
}
