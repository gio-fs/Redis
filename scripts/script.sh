for i in {1..1000} 
	do 
		/root/Redis/obj/client set bri_$i $i && /root/Redis/obj/client get bri_$i && /root/Redis/obj/client del bri_$i
	done 

