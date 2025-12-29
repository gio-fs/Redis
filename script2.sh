for i in {1..100} 
do
	./client set br-$i T$i
done

./client keys
