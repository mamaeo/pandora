# pandora

## Progetto
Il progetto prevede la realizzazione di un vaso dotato di sensori, led e una pompa per l'irrigazione. L'utente ha la possibilità di interagire con esso attraverso il protocollo MQTT per ricevere aggiornamenti sullo stato della pianta e poter impartire comandi quali accensione LEDs e avvio pompa.
E' inoltre possibile sfruttare un algoritmo (molto semplice) di IA grazie al quale il vaso provvederà a mantenere un livello di umidità del suolo e di luminosità ambientale fissati dall'utente, prendendo in totale autonomia le decisioni su quando sia necessario irrigare o fornire maggiore illuminazione alla pianta.

- ## Sensori
  I sensori utilizzati per la raccolta dei dati sono:
    - Fotoresistenza 
    - DHT11 per il rilevamento dell'umidità ambientale e temperatura
    - Sensore di umidità del suolo
    - Interruttore a galleggiante per misurare la presenza di acqua sufficiente

- ## Attuatori
  Gli attuatori utilizzati per l'interazione con l'ambiente sono:
    - LEDs per l'illuminazione
    - Pompa a 5v per l'irrigazione

## Codice
La struttura del progetto è composta da diverse directory:
```
| pandora
|-- README.md
|-- LICENSE
|-- arduino
|   |-- pandora.ino
|-- fritzing
|   |-- pandora.fzz
|   |-- circuit.png
```

