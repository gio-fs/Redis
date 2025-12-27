for i in {1..1000} 
	do 
		./client set bri_$i $i && ./client get bri_$i && ./client del bri_$i
	done 

