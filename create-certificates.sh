#!/bin/bash
set -euo pipefail
IFS=$'\n\t'

# Nom DNS du serveur
HOST=cchat

# Adresses IP
IPS=(127.0.0.1)

ROOT_DIR=ssl

# Server key file
SERVER_KEY=server-key.pem

# Server certificate
SERVER_CERTIFICATE=server-cert.pem

# CA key file
CA_KEY=ca-key.pem

# CA Root certificate
CA_CERTIFICATE=ca-cert.pem

########## FIN DE LA CONFIGURATION ##########

cd $ROOT_DIR

# Récupère le mot de passe de la clé du CA
read -s -p "Mot de passe de la clé du serveur : " CA_KEY_PASSPHRASE
echo

# Vérifie si un certificat est encore valide
is_valid() {
  openssl x509 -noout -checkend 0 -in $1
}

# Crée la clé privée pour le CA
create_ca_key() {
  echo "Création de la clé privée pour le CA ..."
  openssl genrsa -aes256 -passout pass:$CA_KEY_PASSPHRASE -out $CA_KEY 4096
  chmod 0400 $CA_KEY
  echo "Clé privée créée dans le fichier $CA_KEY"
}

# Régénère un certificat root
update_root_ca() {
  echo "Mise à jour du certificat root CA ..."

  rm -f $CA_CERTIFICATE

  # 15330 jours = 420 ans
  openssl req -new -x509 -days 153300 -key $CA_KEY -passout pass:$CA_KEY_PASSPHRASE -subj '/CN=Scantor Root CA/C=FR' -sha256 -out $CA_CERTIFICATE
  chmod 0400 $CA_CERTIFICATE
  echo "Certificat root CA créé dans le fichier $CA_CERTIFICATE"
}

create_server_key() {
  echo "Creation de la clé privée du serveur ..."
  openssl genrsa -out $SERVER_KEY 4096
  chmod 0400 $SERVER_KEY
  echo "Clé privée créée dans le fichier $SERVER_KEY"
}

update_server_cert() {
  echo "Mise à jour du certificat serveur ..."

  rm -f $SERVER_CERTIFICATE

  openssl req -subj "/CN=$HOST" -sha256 -new -key $SERVER_KEY -out server.csr
  echo subjectAltName = DNS:$HOST,IP:127.0.0.1 >> extfile.cnf
  echo extendedKeyUsage = serverAuth >> extfile.cnf

  openssl x509 -req -days 153300 -sha256 -in server.csr -CA $CA_CERTIFICATE -CAkey $CA_KEY \
  -CAcreateserial -out $SERVER_CERTIFICATE -extfile extfile.cnf -passin pass:$CA_KEY_PASSPHRASE
  rm -f extfile.cnf server.csr
  chmod 0400 $SERVER_CERTIFICATE

  echo "Certificat serveur créé dans le fichier $SERVER_CERTIFICATE"
}

# Crée la clé privée pour le CA si elle n'existe pas
if [[ ! -e $CA_KEY ]]; then
  create_ca_key

  # Si la clé privée à été créée, on doit regénérer tous les certificats
  rm -f $CA_CERTIFICATE $SERVER_CERTIFICATE
fi

if [[ ! -e $CA_CERTIFICATE || ! $(is_valid $CA_CERTIFICATE) ]]; then
  update_root_ca
fi

if [[ ! -e $SERVER_KEY ]]; then
  create_server_key

  # Si la clé privée à été créée, on doit regénérer le certificat
  rm -f $SERVER_CERTIFICATE
fi

if [[ ! -e $SERVER_CERTIFICATE || ! $(is_valid $SERVER_CERTIFICATE) ]]; then
  update_server_cert
fi

echo Fini !
