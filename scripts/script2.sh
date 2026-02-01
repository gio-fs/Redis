for i in {1..100} 
do
	/root/Redis/obj/client set br-$i T$i
done

/root/Redis/obj/client keys
