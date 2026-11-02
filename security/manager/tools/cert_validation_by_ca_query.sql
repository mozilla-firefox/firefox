-- This is a generated file. To regenerate, run regen_root_ca_metadata.py.
WITH per_client AS (
  SELECT
    client_info.client_id,
    DATE(submission_timestamp) AS day,
    CAST(v.key AS INT64) AS ca,
    SUM(v.value) AS value
  FROM
    `moz-fx-data-shared-prod.firefox_desktop.metrics`,
    UNNEST(metrics.custom_distribution.cert_validation_success_by_ca_2.values) AS v
  WHERE
    client_info.app_channel = 'release'
    AND DATE(submission_timestamp) >= DATE_SUB(CURRENT_DATE, INTERVAL 7 DAY)
    AND metrics.custom_distribution.cert_validation_success_by_ca_2.values IS NOT NULL
    AND sample_id < 10 -- 10% sample; use sample_id = 0 for 1%, remove for 100%
  GROUP BY
    client_info.client_id,
    day,
    ca
),
totals AS (
  SELECT
    ca,
    SUM(value) AS value,
    day
  FROM
    per_client
  GROUP BY
    ca,
    day
)
SELECT
  CASE ca
    WHEN 4 THEN 'DigiCert TLS ECC P384 Root G5 (4)'
    WHEN 5 THEN 'CommScope Public Trust RSA Root-01 (5)'
    WHEN 6 THEN 'Entrust Root Certification Authority - EC1 (6)'
    WHEN 7 THEN 'AffirmTrust Commercial (7)'
    WHEN 8 THEN 'D-TRUST BR Root CA 2 2023 (8)'
    WHEN 9 THEN 'D-TRUST EV Root CA 1 2020 (9)'
    WHEN 10 THEN 'AffirmTrust Networking (10)'
    WHEN 11 THEN 'COMODO Certification Authority (11)'
    WHEN 12 THEN 'CommScope Public Trust ECC Root-01 (12)'
    WHEN 13 THEN 'emSign Root CA - C1 (13)'
    WHEN 14 THEN 'COMODO ECC Certification Authority (14)'
    WHEN 15 THEN 'GlobalSign ECC Root CA - R5 (15)'
    WHEN 16 THEN 'Amazon Root CA 3 (16)'
    WHEN 17 THEN 'QuoVadis Root CA 3 (17)'
    WHEN 18 THEN 'Amazon Root CA 2 (18)'
    WHEN 19 THEN 'SSL.com EV Root Certification Authority ECC (19)'
    WHEN 20 THEN 'Telia Root CA v2 (20)'
    WHEN 21 THEN 'Izenpe.com (21)'
    WHEN 22 THEN 'GlobalSign Root CA - R6 (22)'
    WHEN 23 THEN 'Starfield Root Certificate Authority - G2 (23)'
    WHEN 24 THEN 'TunTrust Root CA (24)'
    WHEN 25 THEN 'SSL.com EV Root Certification Authority RSA R2 (25)'
    WHEN 26 THEN 'CommScope Public Trust ECC Root-02 (26)'
    WHEN 27 THEN 'IdenTrust Public Sector Root CA 1 (27)'
    WHEN 28 THEN 'vTrus ECC Root CA (28)'
    WHEN 29 THEN 'DigiCert Global Root G3 (29)'
    WHEN 30 THEN 'SSL.com Root Certification Authority ECC (30)'
    WHEN 31 THEN 'GTS Root R4 (31)'
    WHEN 32 THEN 'GTS Root R3 (32)'
    WHEN 33 THEN 'Microsoft ECC Root Certificate Authority 2017 (33)'
    WHEN 34 THEN 'DigiCert TLS RSA4096 Root G5 (34)'
    WHEN 35 THEN 'Microsec e-Szigno Root CA 2009 (35)'
    WHEN 36 THEN 'DigiCert Assured ID Root CA (36)'
    WHEN 37 THEN 'SecureSign Root CA12 (37)'
    WHEN 38 THEN 'TWCA CYBER Root CA (38)'
    WHEN 39 THEN 'HARICA TLS ECC Root CA 2021 (39)'
    WHEN 40 THEN 'emSign Root CA - G1 (40)'
    WHEN 41 THEN 'Secure Global CA (41)'
    WHEN 42 THEN 'DigiCert Global Root CA (42)'
    WHEN 43 THEN 'Entrust Root Certification Authority - G2 (43)'
    WHEN 44 THEN 'Hellenic Academic and Research Institutions ECC RootCA 2015 (44)'
    WHEN 45 THEN 'Go Daddy Root Certificate Authority - G2 (45)'
    WHEN 46 THEN 'TUBITAK Kamu SM SSL Kok Sertifikasi - Surum 1 (46)'
    WHEN 47 THEN 'D-TRUST Root Class 3 CA 2 2009 (47)'
    WHEN 48 THEN 'SecureSign Root CA14 (48)'
    WHEN 49 THEN 'GlobalSign Root R46 (49)'
    WHEN 50 THEN 'USERTrust ECC Certification Authority (50)'
    WHEN 51 THEN 'Security Communication RootCA2 (51)'
    WHEN 52 THEN 'COMODO RSA Certification Authority (52)'
    WHEN 53 THEN 'DigiCert Trusted Root G4 (53)'
    WHEN 54 THEN 'AC RAIZ FNMT-RCM SERVIDORES SEGUROS (54)'
    WHEN 55 THEN 'Trustwave Global ECC P384 Certification Authority (55)'
    WHEN 56 THEN 'Actalis Authentication Root CA (56)'
    WHEN 57 THEN 'Starfield Services Root Certificate Authority - G2 (57)'
    WHEN 58 THEN 'BJCA Global Root CA2 (58)'
    WHEN 59 THEN 'Telekom Security TLS ECC Root 2020 (59)'
    WHEN 60 THEN 'Autoridad de Certificacion Firmaprofesional CIF A62634068 (60)'
    WHEN 61 THEN 'TWCA Global Root CA (61)'
    WHEN 62 THEN 'Hongkong Post Root CA 3 (62)'
    WHEN 63 THEN 'Certum Trusted Network CA (63)'
    WHEN 64 THEN 'CFCA EV ROOT (64)'
    WHEN 65 THEN 'IdenTrust Commercial Root CA 1 (65)'
    WHEN 66 THEN 'SwissSign Gold CA - G2 (66)'
    WHEN 67 THEN 'certSIGN Root CA G2 (67)'
    WHEN 68 THEN 'ISRG Root X2 (68)'
    WHEN 69 THEN 'Certum EC-384 CA (69)'
    WHEN 70 THEN 'OISTE WISeKey Global Root GB CA (70)'
    WHEN 71 THEN 'NetLock Arany (Class Gold) Főtanúsítvány (71)'
    WHEN 72 THEN 'AffirmTrust Premium (72)'
    WHEN 73 THEN 'Entrust Root Certification Authority (73)'
    WHEN 74 THEN 'DigiCert High Assurance EV Root CA (74)'
    WHEN 75 THEN 'Certainly Root R1 (75)'
    WHEN 76 THEN 'Sectigo Public Server Authentication Root R46 (76)'
    WHEN 77 THEN 'DigiCert Assured ID Root G2 (77)'
    WHEN 78 THEN 'DigiCert Assured ID Root G3 (78)'
    WHEN 79 THEN 'Atos TrustedRoot Root CA RSA TLS 2021 (79)'
    WHEN 80 THEN 'OISTE WISeKey Global Root GC CA (80)'
    WHEN 81 THEN 'SSL.com Root Certification Authority RSA (81)'
    WHEN 82 THEN 'QuoVadis Root CA 2 (82)'
    WHEN 83 THEN 'emSign ECC Root CA - G3 (83)'
    WHEN 84 THEN 'QuoVadis Root CA 3 G3 (84)'
    WHEN 85 THEN 'NAVER Global Root Certification Authority (85)'
    WHEN 86 THEN 'vTrus Root CA (86)'
    WHEN 87 THEN 'QuoVadis Root CA 1 G3 (87)'
    WHEN 88 THEN 'GTS Root R2 (88)'
    WHEN 89 THEN 'D-TRUST EV Root CA 2 2023 (89)'
    WHEN 90 THEN 'Amazon Root CA 1 (90)'
    WHEN 91 THEN 'SSL.com TLS RSA Root CA 2022 (91)'
    WHEN 92 THEN 'QuoVadis Root CA 2 G3 (92)'
    WHEN 93 THEN 'T-TeleSec GlobalRoot Class 2 (93)'
    WHEN 94 THEN 'Trustwave Global ECC P256 Certification Authority (94)'
    WHEN 95 THEN 'ISRG Root X1 (95)'
    WHEN 96 THEN 'Trustwave Global Certification Authority (96)'
    WHEN 97 THEN 'Buypass Class 2 Root CA (97)'
    WHEN 98 THEN 'GLOBALTRUST 2020 (98)'
    WHEN 99 THEN 'ACCVRAIZ1 (99)'
    WHEN 100 THEN 'UCA Global G2 Root (100)'
    WHEN 101 THEN 'Hellenic Academic and Research Institutions RootCA 2015 (101)'
    WHEN 102 THEN 'SZAFIR ROOT CA2 (102)'
    WHEN 103 THEN 'GlobalSign ECC Root CA - R4 (103)'
    WHEN 104 THEN 'Atos TrustedRoot Root CA ECC TLS 2021 (104)'
    WHEN 105 THEN 'Certainly Root E1 (105)'
    WHEN 106 THEN 'Certum Trusted Network CA 2 (106)'
    WHEN 107 THEN 'emSign ECC Root CA - C3 (107)'
    WHEN 108 THEN 'AffirmTrust Premium ECC (108)'
    WHEN 109 THEN 'TrustAsia Global Root CA G4 (109)'
    WHEN 110 THEN 'e-Szigno Root CA 2017 (110)'
    WHEN 111 THEN 'FIRMAPROFESIONAL CA ROOT-A WEB (111)'
    WHEN 112 THEN 'TWCA Root Certification Authority (112)'
    WHEN 113 THEN 'GDCA TrustAUTH R5 ROOT (113)'
    WHEN 114 THEN 'SSL.com TLS ECC Root CA 2022 (114)'
    WHEN 115 THEN 'Microsoft RSA Root Certificate Authority 2017 (115)'
    WHEN 116 THEN 'Sectigo Public Server Authentication Root E46 (116)'
    WHEN 117 THEN 'DigiCert Global Root G2 (117)'
    WHEN 118 THEN 'GlobalSign Root CA - R3 (118)'
    WHEN 119 THEN 'GlobalSign Root E46 (119)'
    WHEN 120 THEN 'UCA Extended Validation Root (120)'
    WHEN 121 THEN 'Certigna Root CA (121)'
    WHEN 122 THEN 'GTS Root R1 (122)'
    WHEN 123 THEN 'HARICA TLS RSA Root CA 2021 (123)'
    WHEN 124 THEN 'TeliaSonera Root CA v1 (124)'
    WHEN 125 THEN 'TrustAsia Global Root CA G3 (125)'
    WHEN 126 THEN 'CA Disig Root R2 (126)'
    WHEN 127 THEN 'Amazon Root CA 4 (127)'
    WHEN 128 THEN 'Certigna (128)'
    WHEN 129 THEN 'D-TRUST BR Root CA 1 2020 (129)'
    WHEN 130 THEN 'Security Communication ECC RootCA1 (130)'
    WHEN 131 THEN 'SecureSign Root CA15 (131)'
    WHEN 132 THEN 'USERTrust RSA Certification Authority (132)'
    WHEN 133 THEN 'certSIGN ROOT CA (133)'
    WHEN 134 THEN 'AC RAIZ FNMT-RCM (134)'
    WHEN 135 THEN 'Buypass Class 3 Root CA (135)'
    WHEN 136 THEN 'D-TRUST Root Class 3 CA 2 EV 2009 (136)'
    WHEN 137 THEN 'Telekom Security TLS RSA Root 2023 (137)'
    WHEN 138 THEN 'HiPKI Root CA - G1 (138)'
    WHEN 139 THEN 'SecureTrust CA (139)'
    WHEN 140 THEN 'Atos TrustedRoot 2011 (140)'
    WHEN 141 THEN 'BJCA Global Root CA1 (141)'
    WHEN 142 THEN 'ANF Secure Server Root CA (142)'
    WHEN 143 THEN 'T-TeleSec GlobalRoot Class 3 (143)'
    WHEN 144 THEN 'Certum Trusted Root CA (144)'
    WHEN 145 THEN 'CommScope Public Trust RSA Root-02 (145)'
    WHEN 146 THEN 'ePKI Root Certification Authority (146)'
    WHEN 147 THEN 'TrustAsia TLS RSA Root CA (147)'
    WHEN 148 THEN 'SwissSign RSA TLS Root CA 2022 - 1 (148)'
    WHEN 149 THEN 'TrustAsia TLS ECC Root CA (149)'
    WHEN 150 THEN ' OISTE Server Root RSA G1 (150)'
    WHEN 151 THEN 'OISTE Server Root ECC G1 (151)'
    WHEN 152 THEN 'e-Szigno TLS Root CA 2023 (152)'
    WHEN 153 THEN 'Telia EC TLS Root CA v3 (153)'
    WHEN 154 THEN 'SECOM TLS RSA Root CA 2024 (154)'
    WHEN 155 THEN 'SECOM TLS ECC Root CA 2024 (155)'
    WHEN 156 THEN 'Telia RSA TLS Root CA v3 (156)'
    WHEN 0 THEN 'Unknown (0)'
    WHEN 1 THEN 'User cert DB (1)'
    WHEN 2 THEN 'External PKCS#11 module (2)'
    WHEN 3 THEN 'Third-party OS root (3)'
    ELSE CAST(ca AS STRING)
  END AS ca,
  value,
  ROUND(100.0 * value / SUM(value) OVER (PARTITION BY day), 2) AS pct,
  day
FROM
  totals
ORDER BY
  day,
  value DESC
