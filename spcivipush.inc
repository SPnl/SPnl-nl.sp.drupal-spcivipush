<?php

/**
 * Parses submission data with sync info in raw data array to sync.
 */
function spcivipush_parse_data($data) {
  $push_items = spcivipush_get_data_names(); 
  foreach ($push_items as $key => $name) {
    if (isset($data[$key])) {
      $validated_value = call_user_func('spcivipush_valid_' . $key, $data[$key]);
      if (!empty($validated_value)) {
        $validated_data[$key] = $validated_value;
      }
    }
  }
  // If possible, split house number in house_number and addition.
  if (!empty($validated_data['house_number'])) {
    $split_house_number = spcivipush_split_house_number($validated_data['house_number']);
    if (!empty($split_house_number[2])) {
      $validated_data['house_number'] = $split_house_number[1];
      if (empty($validated_data['house_number_addition'])) {
        $validated_data['house_number_addition'] = trim($split_house_number[2]);
      }
    }
  }
  // If name item, then if possible, set first name, middle_name, and last name.
  if (!empty($validated_data['name'])) {
    // Check if name contains prepositon.
    $split_name = spcivipush_split_name($validated_data['name']);
    $parts = array('first_name', 'middle_name', 'last_name');
    foreach ($parts as $part) {
      if (isset($split_name[$part])) {
        unset($validated_data['name']);
        $validated_data[$part] = $split_name[$part];
      }
    }
  }
  return $validated_data;
}

/**
 * Checks wether a submission contains sufficient data to create new or sync existing civi contact.
 */
function spcivipush_check_submission_sufficient($data) {
  // Check if values are sufficient to create new or sync existing civi contact.
  if 
    (
      !empty($data['contact_id']) ||
      (
        !empty($data['email']) ||
        !empty($data['telephone']) ||
        (
          !empty($data['street_and_number']) &&
          !empty($data['locality'])

        ) ||
        (
          !empty($data['house_number']) &&
          (
            (
              !empty($data['street']) &&
              !empty($data['locality'])
            ) ||
            !empty($data['postal_code'])
          )
        )
      )
    ) {
    return TRUE;
  }
  watchdog('spcivipush', '<p>Contact kon niet worden aangemaakt, te weinig data.</p><pre>'.print_r($data, TRUE).'</pre>');
  drupal_set_message('Contact kan niet worden gemaakt!', 'error');
  return FALSE;
}

function spcivipush_sync_contact($data, $groups, $debug_info, $source) {
  // Find similar existing contacts in civi.
  $same_contacts = spcivipush_find_same_contacts($data, $debug_info);
  if ($same_contacts !== FALSE) {
    // Sync contact.
    if (empty($same_contacts) || count($same_contacts) > 1) {
      $contact = spcivipush_add_contact($data, $debug_info, $source);
      if (count($same_contacts) > 1) {
        $merge_email = variable_get('spcivipush_merge_contact_email_address', '');
        if (!empty($merge_email)) {
          // Send mail for manually merging.
          $params = array(
            'contact_id' => $contact,
          );
          $message = drupal_mail('spcivipush', 'manual_merge', $merge_email, language_default(), $params, 'webmaster@sp.nl');
          if (!empty($message['result'])) { 
            watchdog('spcivipush', 'Contact handmatig samenvoegen, mail verzonden.');
            drupal_set_message('Contact handmatig samenvoegen, mail verzonden.');
          }
          else {
            watchdog('spcivipush', 'Contact handmatig samenvoegen, geen mail verzonden verzonden (fout opgetreden).');
            drupal_set_message('Contact handmatig samenvoegen, geen mail verzonden (fout opgetreden).');
          }
        }
        else {
          watchdog('spcivipush', 'Contact handmatig samenvoegen, geen mail verzonden.');
          drupal_set_message('Contact handmatig samenvoegen, geen mail verzonden.');
        }
      }
    }
    else {
      $contact_id = spcivipush_merge_contacts($data, $same_contacts, $debug_info);
    }
    if (!empty($contact_id)) {
      // Add contact to groups.
      $result = spcivipush_contact_add_groups($contact_id, $groups, $debug_info);
      if (!empty($result)) {
        return $contact_id;
      }
    }
  }
  return FALSE;
}

function spcivipush_get_data_names() {
  $data_names = array(
    'contact_id' => 'Contact id',
    'name' => 'gehele naam',
    'first_name' => 'voornaam',
    'middle_name' => 'tussenvoegsel',
    'last_name' => 'achternaam',
    'email' => 'e-mail',
    'telephone' => 'telefoon',
    'street' => 'straat',
    'house_number' => 'huisnummer (inc. toevoeging)',
    'house_number_addition' => 'huisnummer toevoeging',
    'street_and_number' => 'straat en huisnummer',
    'postal_code' => 'postcode',
    'locality' => 'plaats',
  );
  return $data_names;
}

function spcivipush_split_house_number($house_number) {
  $house_number = trim($house_number);
  $regex = "@^([0-9]+)([!0-9].*)$@";
  if (preg_match($regex, $house_number, $matches)) {
    return $matches;
  }
  return FALSE;
}

function spcivipush_split_name($name) {
  $matches = array();
  $name = trim($name);
  $name = trim($name, '.,');

  // Remove comma from name.
  $comma_pos = strpos($name, ',');
  if ($comma_pos !== FALSE) {
    $first_half = trim(substr($name, 0, $comma_pos));
    $last_half = trim(substr($name, $comma_pos + 1));
    $name = $last_half . ' ' . $first_half;
  }

  // Replace terms.
  $replace = array(
    ' v. ' => ' van ',
    ' vd. ' => ' van de ',
    ' vd ' => ' van de ',
    ' v/d ' => ' van de ',
  );
  foreach ($replace as $orig => $new) {
    if (strpos($name, ' '.$orig.' ') !== FALSE) {
      $name = str_replace($orig, $new, $name);
    }
  }

  // Regexes for title and middle names.
  $titles_regex = 'Mr|mr|Mevr|mevr|Dhr|dhr|Ir|ir|Ing|ing|Bc|bc|Ms|ms|Drs|drs|Dr|dr';

  $middle_names_array = spcivipush_middle_names(); 
  $middle_names_regex = implode('|', $middle_names_array);

  // Split name on middle_name.
  $regex = "@^(?'title'(\b(" . $titles_regex . ")\b\.?\s)?)(?'first_name'(([A-ZÀÈÌÒÙÁÉÍÓÚÝÂÊÎÔÛÃÑÕÄËÏÖÜŸÇÅa-zàèìòùáéíóúýâêîôûãñõäëïöü¡¿çãñõ'\-\.]+)\s*)+?)(?'middle_name'(\b(" . $middle_names_regex . ")\b\s*))(?'last_name'(\b[A-ZÀÈÌÒÙÁÉÍÓÚÝÂÊÎÔÛÃÑÕÄËÏÖÜŸÇÅa-zàèìòùáéíóúýâêîôûãñõäëïöü¡¿çãñõ'][a-zàèìòùáéíóúýâêîôûãñõäëïöü¡¿çãñõ'\-]+\b\s?)+)$@";
  $result = preg_match($regex, $name, $matches);
  if (!preg_match($regex, $name, $matches)) {
    // Split name without middle_name in first name and last name(s).
    $regex = "@^(?'title'(\b(" . $titles_regex . ")\b\.?\s)?)(?'first_name'(([A-ZÀÈÌÒÙÁÉÍÓÚÝÂÊÎÔÛÃÑÕÄËÏÖÜŸÇÅa-zàèìòùáéíóúýâêîôûãñõäëïöü¡¿çãñõ'\-\.]+)\s*)+?)(?'last_name'(\b[A-ZÀÈÌÒÙÁÉÍÓÚÝÂÊÎÔÛÃÑÕÄËÏÖÜŸÇÅa-zàèìòùáéíóúýâêîôûãñõäëïöü¡¿çãñõ'][a-zàèìòùáéíóúýâêîôûãñõäëïöü¡¿çãñõ'\-]+\b\s?)+)$@";
    $result = preg_match($regex, $name, $matches);
    if (!preg_match($regex, $name, $matches)) {
      // Check if only first name.
      $regex = "@^(?'first_name'([A-ZÀÈÌÒÙÁÉÍÓÚÝÂÊÎÔÛÃÑÕÄËÏÖÜŸÇÅa-zàèìòùáéíóúýâêîôûãñõäëïöü¡¿çãñõ'][a-zàèìòùáéíóúýâêîôûãñõäëïöü¡¿çãñõ'\-]+))$@";
      $result = preg_match($regex, $name, $matches);
      if (!preg_match($regex, $name, $matches)) {
        // No match found.
        return FALSE;
      }
    }
  }
  // Set first character uppercase.
  $parts = array(
    'first_name',
    'last_name',
  );
  foreach ($parts as $part) {
    if (!empty($matches[$part])) {
      $matches[$part] = ucfirst($matches[$part]);
    }
  }
  return $matches;
}

function spcivipush_valid_contact_id($contact_id) {
  $contact_id = trim($contact_id);
  if (!empty($contact_id)) {
    if (is_numeric($contact_id) && intval($contact_id) == $contact_id && $contact_id > 0) {
      return $contact_id;
    }
  }
  return FALSE;
}

function spcivipush_valid_name($name) {
  $name = trim($name);
  if (!empty($name)) return $name;
}

function spcivipush_valid_first_name($first_name) {
  $first_name = trim($first_name);
  if (!empty($first_name)) return $first_name;
}

function spcivipush_valid_middle_name($middle_name) {
  $middle_name = trim($middle_name);
  if (!empty($middle_name)) return $middle_name;
}

function spcivipush_valid_last_name($last_name) {
  $last_name = trim($last_name);
  if (!empty($last_name)) return $last_name;
}
function spcivipush_valid_email($email) {
  $email = trim($email);
  if (valid_email_address($email)) return strtolower($email);
}

function spcivipush_valid_telephone($telephone) {
  if (!empty($telephone)) {
    $telephone = preg_replace('@[^0-9]@', '', $telephone);
    // +31612345678 => 0649492174
    if (
      strlen($telephone) === 11 &&
      strpos($telephone, '31') === 0
    ) {
      $telephone = substr_replace($telephone, '0', 0, 2);
    }
    // 0031612345678 => 0649492174
    if (
      strlen($telephone) === 13 &&
      strpos($telephone, '0031') === 0
    ) {
      $telephone = substr_replace($telephone, '0', 0, 4);
    }
    return $telephone;
  }
}

function spcivipush_valid_street($street) {
  $street = trim($street);
  $street = ucfirst($street);
  if (!empty($street)) return $street;
}

function spcivipush_valid_house_number($house_number) {
  // Allow house number and addition, split when parsing data.
  $house_number = trim($house_number);
  if (!empty($house_number)) return $house_number;
}

function spcivipush_valid_house_number_addition($house_number_addition) {
  $house_number_addition = trim($house_number_addition);
  if (!empty($house_number_addition)) return $house_number_addition;
}

function spcivipush_valid_street_and_number($street_and_number) {
  $street_and_number = trim($street_and_number);
  $street_and_number = ucfirst($street_and_number);
  if (!empty($street_and_number)) return $street_and_number;
}

function spcivipush_valid_locality($locality) {
  $locality = trim($locality);
  $locality = ucfirst($locality);
  if (!empty($locality)) return $locality;
}

function spcivipush_valid_postal_code($postal_code) {
  $postal_code = trim($postal_code);
  if (preg_match('@^([1-9][0-9]{3})\s?([a-zA-Z]{2})$@', $postal_code, $matches)) {
    return $matches[1] . ' ' . strtoupper($matches[2]);
  }
}


function spcivipush_middle_names() {
  return array("‘S","‘s","‘T","‘t","A","a","Aan","aan","Aan ‘t","aan ‘t","Aan de","aan de","Aan den","aan den","Aan der","aan der","Aan het","aan het","Aan t","aan t","Af","af","Al","al","Am","am","Am de","am de","Auf","auf","Auf dem","auf dem","Auf den","auf den","Auf der","auf der","Auf ter","auf ter","Aus","aus","Aus ‘m","aus ‘m","Aus dem","aus dem","Aus den","aus den","Aus der","aus der","Aus m","aus m","Ben","ben","Bij","bij","Bij ‘t","bij ‘t","Bij de","bij de","Bij den","bij den","Bij het","bij het","Bij t","bij t","Bin","bin","Boven d","boven d","Boven d’","boven d’","D","d","D’","d’","Da","da","Dal","dal","Dal’","dal’","Dalla","dalla","Das","das","De","de","De die","de die","De die le","de die le","De l","de l","De l’","de l’","De la","de la","De las","de las","De le","de le","De van der","de van der","Deca","deca","Degli","degli","Dei","dei","Del","del","Della","della","Den","den","Der","der","Des","des","Di","di","Die le","die le","Do","do","Don","don","Dos","dos","Du","du","El","el","Het","het","I","i","Im","im","In","in","In ‘t","in ‘t","In de","in de","In den","in den","In der","in der","In het","in het","In t","in t","L","l","L’","l’","La","la","Las","las","Le","le","Les","les","Lo","lo","Los","los","Of","of","Onder","onder","Onder ‘t","onder ‘t","Onder de","onder de","Onder den","onder den","Onder het","onder het","Onder t","onder t","Op","op","Op ‘t","op ‘t","Op de","op de","Op den","op den","Op der","op der","Op gen","op gen","Op het","op het","Op t","op t","Op ten","op ten","Over","over","Over ‘t","over ‘t","Over de","over de","Over den","over den","Over het","over het","Over t","over t","S","s","S’","s’","T","t","Te","te","Ten","ten","Ter","ter","Tho","tho","Thoe","thoe","Thor","thor","To","to","Toe","toe","Tot","tot","Uijt","uijt","Uijt ‘t","uijt ‘t","Uijt de","uijt de","Uijt den","uijt den","Uijt te de","uijt te de","Uijt ten","uijt ten","Uit","uit","Uit ‘t","uit ‘t","Uit de","uit de","Uit den","uit den","Uit het","uit het","Uit t","uit t","Uit te de","uit te de","Uit ten","uit ten","Unter","unter","Van","van","Van ‘t","van ‘t","Van de","van De","van de","Van de l","van de l","Van de l’","van de l’","Van Den","Van den","van den","Van Der","Van der","van der","Van gen","van gen","Van het","van het","Van la","van la","Van t","van t","Van ter","van ter","Van van de","van van de","Ver","ver","Vom","vom","Von","von","Von ‘t","von ‘t","Von dem","von dem","Von den","von den","Von der","von der","Von t","von t","Voor","voor","Voor ‘t","voor ‘t","Voor de","voor de","Voor den","voor den","Voor in ‘t","voor in ‘t","Voor in t","voor in t","Vor","vor","Vor der","vor der","Zu","zu","Zum","zum","Zur","zur");
}

function spcivipush_find_same_contacts($data, $debug_info) {
  $spcivi = \SPCivi::getInstance();
  set_time_limit(60);
  $same_contacts = array();

  // Find same contact based on contact_id.
  if (isset($data['contact_id'])) {
    $contact_id = $data['contact_id'];
    $params = array(
      'option.limit' => 1,
      'contact_id' => $contact_id,
      'contact_is_deleted' => 0,
    );
    $contact_results = $spcivi->api('Contact', 'get', $params);
    if (_spcivipush_check_result($contact_results, 'een contact', 'get', $params, $debug_info)) {
      if (!empty($contact_results['values'])) {
        $same_contacts[$contact_id] = $contact_id;
        return $same_contacts;
      }
    }
  }

  // Find same contacts based on mobile phone.
  if (
    isset($data['telephone']) &&
    strpos($data['telephone'], '06') === 0 &&
    (
      isset($data['last_name']) ||
      isset($data['first_name'])
    )
  ) {
    $same_contacts_phone = array();
    $mobile_phone = $data['telephone'];
    $params = array(
      'option.limit' => 5,
    );
    // Also check international number.
    $mobile_phone_int = '0031' . substr($mobile_phone, 1);
    foreach (array($mobile_phone, $mobile_phone_int) as $number_to_check) {
      $params['phone_numeric'] = $number_to_check;
      $mobile_phone_results = $spcivi->api('Phone', 'get', $params);
      if (_spcivipush_check_result($mobile_phone_results, 'telefoonnummers', 'get', $params, $debug_info)) {
        if (!empty($mobile_phone_results['values'])) {
          foreach ($mobile_phone_results['values'] as $value) {
            $same_contacts_phone[$value['contact_id']] = $value['contact_id'];
          }
        }
      }
      else {
        return FALSE;
      }
    }
    // check if found contacts have same last name.
    foreach ($same_contacts_phone as $phone_contact_id) {
      // Get contact information.
      $params = array(
        'contact_id' => $phone_contact_id,
        'contact_is_deleted' => 0,
      );
      $contact_results = $spcivi->api('Contact', 'get', $params);
      if (_spcivipush_check_result($contact_results, 'een contact', 'get', $params, $debug_info)) {
        if (!empty($contact_results['values'])) {
          // Check if first name and last name are identical.
          $contact = array_pop($contact_results['values']);
          if (
            (
              isset($data['last_name']) &&
              strpos(strtolower($contact['display_name']), strtolower($data['last_name'])) !== FALSE
            ) ||
            (
              isset($data['first_name']) &&
              strpos(strtolower($contact['display_name']), strtolower($data['first_name'])) !== FALSE
            )
          ) {
            $same_contacts[$phone_contact_id] = $phone_contact_id;
          }
        }
      }
      else {
        return FALSE;
      }
    }
  }

  // Find similar contacts based on email, first- and last name.
  if (
    isset($data['email']) &&
    isset($data['first_name']) &&
    isset($data['last_name'])
  ) {
    $params = array(
      'option.limit' => 5,
      'email' => $data['email'],
    );
    $email_results = $spcivi->api('Email', 'get', $params);
    if (_spcivipush_check_result($email_results, 'e-mailadressen', 'get', $params, $debug_info)) {
      if (!empty($email_results['values'])) {
        foreach ($email_results['values'] as $value) {
          $email_contact_ids[$value['contact_id']] = $value['contact_id'];
        }
        foreach ($email_contact_ids as $contact_id) {
          // Get contact information.
          $params = array(
            'contact_id' => $contact_id,
            'contact_is_deleted' => 0,
          );
          $contact_results = $spcivi->api('Contact', 'get', $params);
          if (_spcivipush_check_result($contact_results, 'een contact', 'get', $params, $debug_info)) {
            if (!empty($contact_results['values'])) {
              // Check if first name and last name are identical.
              $contact = array_pop($contact_results['values']);
              if (
                $data['first_name'] === $contact['first_name'] &&
                $data['last_name'] === $contact['last_name']
              ) {
                $same_contacts[$contact_id] = $contact_id;
              }
            }
          }
          else {
            return FALSE;
          }
        }
      }
    }
    else {
      return FALSE;
    }
  }
  return $same_contacts;
}

function spcivipush_add_contact($data, $debug_info, $source) {
  global $base_url;
  $spcivi = \SPCivi::getInstance();
  set_time_limit(60);
  $sync_items = spcivipush_get_data_names(); 

  // Create contact.
  // Autosync fields than can be autosynced.
  $autosync = array(
    'first_name' => 'first_name',
    'middle_name' => 'middle_name',
    'last_name' => 'last_name',
    'email' => 'email',
    'source' => 'contact_source',
  );
  $params = array(
    'contact_type' => 'Individual',
    'contact_source' => $source,
  );
  foreach ($autosync as $key => $civikey) {
    if (!empty($data[$key])) {
      $params[$civikey] = $data[$key];
      unset($sync_items[$key]);
    }
  }
  // Make sure last name is set, if data is available.
  if (empty($data['first_name']) && empty($data['last_name']) && !empty($data['name'])) {
    $params['last_name'] = $data['name'];
    $check_name = TRUE; // Create contact first for id.
  }
  unset($sync_items['name']);
  $create_contact_results = $spcivi->api('Contact', 'create', $params);
  if (_spcivipush_check_result($create_contact_results, 'een contact', 'create', $params, $debug_info)) {
    $contact_id = $create_contact_results['id'];
    drupal_set_message('Contact in CiviCRM toegevoegd: ' . $contact_id);
    watchdog('spcivipush', 'Contact in CiviCRM toegevoegd: ' . $contact_id);

    // If no split name, send mail for manually checking name.
    if (!empty($check_name)) {
      $check_name_email = variable_get('spcivipush_check_name_email_address', '');
      if (!empty($check_name_email)) {
        $params = array(
          'contact_id' => $contact_id,
          'name' => $data['name'],
        );
        $message = drupal_mail('spcivipush', 'check_name', $check_name_email, language_default(), $params, 'webmaster@sp.nl');
      }
    }

    // If available, add phone.
    if (!empty($data['telephone'])) {
      if (strpos($data['telephone'], '06') === 0) {
        $phone_type_id = 2;
      }
      else {
        $phone_type_id = 1;
      }
      $params = array(
        'contact_id' => $contact_id,
        'location_type_id' => 3,
        'phone' => $data['telephone'],
        'is_primary' => 1,
        'phone_type_id' => $phone_type_id,
      );
      $create_phone_result = $spcivi->api('Phone', 'create', $params);
      if (!_spcivipush_check_result($create_phone_result, 'een telefoonnummer', 'create', $params, $debug_info)) {
        return FALSE;
      }
    }
    // If available, add address.
    $autosync = array(
      'street_and_number' => 'street_address',
      'street' => 'street_name',
      'house_number' => 'street_number',
      'house_number_addition' => 'street_unit',
      'postal_code' => 'postal_code',
      'locality' => 'city',
    );
    $params = array(
      'contact_id' => $contact_id,
      'location_type_id' => 1,
      'is_primary' => 1,
      'country_id' => 1152,
    );
    $address_data_found = FALSE;
    foreach ($autosync as $key => $civikey) {
      if (!empty($data[$key])) {
        $params[$civikey] = $data[$key];
        $address_data_found = TRUE;
      }
    }
    if ($address_data_found) {
      $create_address_results = $spcivi->api('Address', 'create', $params);
      if (!_spcivipush_check_result($create_address_results, 'een adres', 'create', $params, $debug_info)) {
        return FALSE;
      }
      else {
        if (!empty($data['street_and_number'])) {
          spcivipush_check_address_manually($data['street_and_number'], $contact_id);
        }
      }
    }
    // If available set unverified tag.
    $tag_id = variable_get('spcivipush_unverified_tag', FALSE);
    if (!empty($tag_id)) {
      $params = array(
        'contact_id' => $contact_id,
        'tag_id' => $tag_id,
      );
      $create_tag_results = $spcivi->api('EntityTag', 'create', $params);
      if (!_spcivipush_check_result($create_tag_results, 'een tag', 'create', $params, $debug_info)) {
        return FALSE;
      }
    }
    return $contact_id;
  }
  return FALSE;
}

function _spcivipush_check_result($result, $dataname, $action, $params = NULL, $debug_info = NULL) {
  $action_name = ($action === 'get') ? 'ophalen' : 'aanmaken';
  $place_name = ($action === 'get') ? 'uit' : 'in';
  if (!isset($result['is_error']) || $result['is_error'] == 1) {
    drupal_set_message('Fout bij het ' . $action_name . ' van ' . $dataname . ' ' . $place_name . ' CiviCRM.', 'error');
    watchdog('spcivisync', '<p>Fout bij het ' . $action_name . ' van ' . $dataname . ' ' . $place_name . ' CiviCRM.</p>Resultaat:<pre>' . print_r($result, TRUE) . '</pre>Parameters:<pre>' . print_r($params, TRUE) . '</pre>Debug info:<pre>' . print_r($debug_info, TRUE) . '</pre>', array(), WATCHDOG_ERROR);
    return FALSE;
  }
  return TRUE;
}

function spcivipush_check_address_manually($street_and_number, $contact_id) {
  // Check if there is a house number addition in a street_address, if so, send mail for manual correction.
  if (!empty($street_and_number)) {
    preg_match('/\d+/', $street_and_number, $m, PREG_OFFSET_CAPTURE);
    if (sizeof($m)) {
      $addition_pos = $m[0][1] + strlen($m[0][0]);
      if ($addition_pos < strlen($street_and_number)) {
        // Addition found, send mail.
        $check_address_email = variable_get('spcivipush_check_address_email_address', '');
        if (!empty($check_address_email)) {
          // Send mail for manually merging.
          $params = array(
            'contact_id' => $contact_id,
            'street_and_number' => $street_and_number,
          );
          $message = drupal_mail('spcivipush', 'check_address', $check_address_email, language_default(), $params, 'webmaster@sp.nl');
          if (!empty($message['result'])) { 
            watchdog('spcivipush', 'Contact controle huisnummer toevoeging, mail verzonden.');
            drupal_set_message('Contact controle huisnummer toevoeging, mail verzonden.');
          }
          else {
            watchdog('spcivipush', 'Contact controle huisnummer toevoeging, geen mail verzonden verzonden (fout opgetreden).');
            drupal_set_message('Contact controle huisnummer toevoeging, geen mail verzonden (fout opgetreden).');
          }
        }
      }
    }
  }
}

function spcivipush_merge_contacts($data, $same_contacts, $debug_info) {
  $merged_data = FALSE;
  $spcivi = \SPCivi::getInstance();
  set_time_limit(60);

  // Get contact.
  $contact_id = array_pop($same_contacts);
  $params = array(
    'id' => $contact_id,
  );
  $get_contact_results = $spcivi->api('Contact', 'get', $params);
  if (_spcivipush_check_result($get_contact_results, 'contacten', 'get', $params, $debug_info)) {
    if (!empty($get_contact_results['values'])) {
      $contact = array_pop($get_contact_results['values']);

      drupal_set_message('Bestaand contact gevonden: ' . $contact_id);
      watchdog('spcivipush', 'Bestaand contact gevonden: ' . $contact_id);

      // Merge name data with contact.
      if (
        empty($contact['first_name']) &&
        empty($contact['last_name']) &&
        (
          !empty($data['first_name']) ||
          !empty($data['last_name']) ||
          !empty($data['name'])
        )
      ) {
        $params = array(
          'id' => $contact_id,
        );
        $autosync = array(
          'first_name' => 'first_name',
          'middle_name' => 'middle_name',
          'last_name' => 'last_name',
        );
        foreach ($autosync as $key => $civikey) {
          if (!empty($data[$key])) {
            $params[$civikey] = $data[$key];
          }
        }
        // Make sure last name is set, if data is available.
        if (empty($data['first_name']) && empty($data['last_name']) && !empty($data['name'])) {
          $params['last_name'] = $data['name'];
        }
        $merge_contact_results = $spcivi->api('Contact', 'create', $params);
        if(_spcivipush_check_result($merge_contact_results, 'contacten', 'create', $params, $debug_info)) {
          $merged_data = TRUE;
        }
        else {
          return FALSE;
        }

      }

      // Merge email data with contact.
      if (!empty($data['email'])) {
        // Check if e-mail is already set.
        $params = array(
          'contact_id' => $contact_id,
          'email' => $data['email']
        );
        $get_emails_result = $spcivi->api('Email', 'get', $params);
        if(!_spcivipush_check_result($get_emails_result, 'een e-mailadres', 'get', $params, $debug_info)) {
          return FALSE;
    }
        if (!empty($get_emails_result) && empty($get_emails_result['values'])) {
          if (empty($contact['email'])) {
            $primary = 1;
            $location_type_id = 3;
          }
          else {
            $primary = 0;
            $location_type_id = 4;
          }
          // Add email.
          $params = array(
            'contact_id' => $contact_id,
            'location_type_id' => $location_type_id,
            'email' => $data['email'],
            'is_primary' => $primary,
          );
          $create_email_result = $spcivi->api('Email', 'create', $params);
          if (_spcivipush_check_result($create_email_result, 'een e-mailadres', 'create', $params, $debug_info)) {
            $merged_data = TRUE;
          }
          else {
            return FALSE;
          }
        }
      }

      // Merge phone data with contact.
      if (!empty($data['telephone'])) {
        // Check if phone is already set.
        $params = array(
          'contact_id' => $contact_id,
          'phone_numeric' => $data['telephone']
        );
        $get_phone_result = $spcivi->api('Phone', 'get', $params);
        if (!_spcivipush_check_result($get_phone_result, 'een telefoonnummer', 'get', $params, $debug_info)) {
          return FALSE;
        }
        if (!empty($get_phone_result) && empty($get_phone_result['values'])) {
          // Phone doesn't exist, add.
          if (empty($contact['phone'])) {
            $primary = 1;
            $location_type_id = 3;
          }
          else {
            $primary = 0;
            $location_type_id = 4;
          }
          if (strpos($data['telephone'], '06') === 0) {
            $phone_type_id = 2;
          }
          else {
            $phone_type_id = 1;
          }
          $params = array(
            'contact_id' => $contact_id,
            'location_type_id' => $location_type_id,
            'phone' => $data['telephone'],
            'is_primary' => $primary,
            'phone_type_id' => $phone_type_id,
          );
          $create_phone_result = $spcivi->api('Phone', 'create', $params);
          if(_spcivipush_check_result($create_phone_result, 'een telefoonnummer', 'create', $params, $debug_info)) {
            $merged_data = TRUE;
          }
          else {
            return FALSE;
          }
        }
      }

      // Merge address data if no address with same house number or postal code exists.
      if (!empty($data['house_number']) || !empty($data['postal_code']) || (!empty($data['street_and_number']) && !empty($data['locality']))) {
        if (!empty($data['house_number'])) {
          // Check if address with same house number exists.
          $params = array(
            'contact_id' => $contact_id,
            'street_number' => $data['house_number'],
          );
          $get_street_number_address_result = $spcivi->api('Address', 'get', $params);
          if(!_spcivipush_check_result($get_street_number_address_result, 'een adres', 'get', $params, $debug_info)) {
            return FALSE;
          }
        }
        if (!empty($data['postal_code'])) {
          // Check if address with same postal code exists.
          $params = array(
            'contact_id' => $contact_id,
            'postal_code' => $data['postal_code'],
          );
          $get_postal_code_address_result = $spcivi->api('Address', 'get', $params);
          if(!_spcivipush_check_result($get_postal_code_address_result, 'een adres', 'get', $params, $debug_info)) {
            return FALSE;
          }
        }
        if (!empty($data['street_and_number']) && !empty($data['locality']) && empty($contact['address_id'])) {
          // Add address.
          $add_address = TRUE;
        }
        else {
          $add_address = FALSE;
        }
        if (
          (
            empty($get_street_number_address_result['values']) &&
            empty($get_postal_code_address_result['values'])
          ) || $add_address
        ) {
          // Add extra address.
          if (empty($contact['address_id'])) {
            $primary = 1;
            $location_type_id = 3;
          }
          else {
            $primary = 0;
            $location_type_id = 4;
          }
          $autosync = array(
            'street_and_number' => 'street_address',
            'street' => 'street_name',
            'house_number' => 'street_number',
            'house_number_addition' => 'street_unit',
            'postal_code' => 'postal_code',
            'locality' => 'city',
          );
          $params = array(
            'contact_id' => $contact_id,
            'location_type_id' => $location_type_id,
            'is_primary' => $primary,
            'country_id' => 1152,
          );
          $address_data_found = FALSE;
          foreach ($autosync as $key => $civikey) {
            if (!empty($data[$key])) {
              $params[$civikey] = $data[$key];
              $address_data_found = TRUE;
            }
          }
          if ($address_data_found) {
            $create_address_results = $spcivi->api('Address', 'create', $params);
            if(_spcivipush_check_result($create_address_results, 'een adres', 'create', $params, $debug_info)) {
              $merged_data = TRUE;
              if (!empty($data['street_and_number'])) {
                spcivipush_check_address_manually($data['street_and_number'], $contact_id);
              }
            }
            else {
              return FALSE;
            }
          }
        }
      }
      if ($merged_data) {
        drupal_set_message('Contactinformatie in CiviCRM samengevoegd met bestaand contact: ' . $contact_id);
        watchdog('spcivipush', 'Contactinformatie in CiviCRM samengevoegd met bestaand contact: ' . $contact_id);
      }
      return $contact_id;
    }
  }
  return FALSE;
}

function spcivipush_contact_add_groups($contact_id, $groups, $debug_info) {
  $added_to_groups = FALSE;
  $spcivi = \SPCivi::getInstance();
  set_time_limit(60);
  // Sync met CiviCRM.
  foreach ($groups as $group_id) {
    $params = array(
      'contact_id' => $contact_id,
      'group_id' => $group_id,
    );
    $create_group_membership = $spcivi->api('GroupContact', 'create', $params);
    if (_spcivipush_check_result($create_group_membership, 'groepslidmaatschappen', 'create', $params, $debug_info)) {
      $added_to_groups = TRUE;
      $added_groups[] = $group_id;
    }
    else {
      return FALSE;
    }
  }
  if ($added_to_groups) {
    $groups_text = implode(', ', $added_groups);
    watchdog('spcivipush', 'Contact nr. '.$contact_id.' was added to groups ('.$groups_text.').');
  }
  return TRUE;
}


