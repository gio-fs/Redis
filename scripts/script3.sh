client=$"/root/Redis/bin/client"

for i in {1..2} 
do
    for j in {1..100}
    do
        $client zadd bri_$i $j.$j briccoT$j
    done
    for k in {1..100}
    do
        $client zquery bri_$i $k.$k briccoT$k 0 2
    done
done

