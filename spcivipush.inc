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
        if (is_string($validated_value)) {
          $validated_data[$key] = substr($validated_value, 0, 64);
        }
        else {
          $validated_data[$key] = $validated_value;
        }
      }
    }
  }
  // If possible, split house number in house_number and addition.
  if (!empty($validated_data['house_number'])) {
    $split_house_number = spcivipush_split_house_number($validated_data['house_number']);
    if (!empty($split_house_number)) {
      $validated_data['house_number'] = $split_house_number[1];
      if (empty($validated_data['house_number_addition'])) {
        $validated_data['house_number_addition'] = $split_house_number[2];
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

function spcivipush_sync_contact($data, $debug_info) {
  // Find similar existing contacts in civi.
  $same_contacts = spcivipush_find_same_contacts($data, $debug_info);
  if ($same_contacts !== FALSE) {
    // Sync contact.
    if (empty($same_contacts)) {
      if (!empty($data['contact_id'])) {
        watchdog('spcivipush', 'Contact nr. ' . $data['contact_id'] . ' kon niet worden gevonden in CiviCRM.');
        drupal_set_message('Contact nr. ' . $data['contact_id'] . ' kon niet worden gevonden in CiviCRM.');
        return 'contact id not found';
      }
      else {
        $contact_id = spcivipush_add_contact($data, $debug_info);
      }
    }
    elseif (count($same_contacts) > 1) {
      $contact_id = spcivipush_merge_contacts($data, $same_contacts, $debug_info);
      $merge_email = variable_get('spcivipush_merge_contact_email_address', '');
      if (!empty($merge_email)) {
        // Send mail for manually merging.
        $params = array(
          'contact_id' => $contact_id,
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
    else {
      $contact_id = spcivipush_merge_contacts($data, $same_contacts, $debug_info);
    }
    if (!empty($contact_id)) {
      if (!empty($data['groups'])) {
        // Add contact to groups.
        $result = spcivipush_contact_add_groups($contact_id, $data['groups'], $debug_info);
        if (!empty($result)) {
          return $contact_id;
        }
      }
      else {
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
    'notes' => 'notities',
    'overwrite' => 'overschrijven',
    'author' => 'auteur',
    'source' => 'bron',
    'groups' => 'groepen',
  );
  return $data_names;
}

function spcivipush_split_house_number($house_number) {
  $house_number = trim($house_number);
  $regex = "@^([0-9]+)([^0-9].*)$@";
  if (preg_match($regex, $house_number, $matches)) {
    $matches[2] = trim($matches[2]);
    if (!empty($matches[2])) {
      return $matches;
    }
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
    'v' => 'van',
    'v.' => 'van',
    'vd' => 'van de',
    'vd.' => 'van de',
    'v.d.' => 'van de',
    'v/d' => 'van de',
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
  $regex = "@^(?'title'(\b(" . $titles_regex . ")\b\.?\s)?)(?'first_name'(([A-ZÀÈÌÒÙÁÉÍÓÚÝÂÊÎÔÛÃÑÕÄËÏÖÜŸÇÅa-zàèìòùáéíóúýâêîôûãñõäëïöü¡¿çãñõ'\-\.]+)\s*)+?)(?'middle_name'(\b(" . $middle_names_regex . ")\b\s*))(?'last_name'(\b[A-ZÀÈÌÒÙÁÉÍÓÚÝÂÊÎÔÛÃÑÕÄËÏÖÜŸÇÅa-zàèìòùáéíóúýâêîôûãñõäëïöü¡¿çãñõ'][a-zàèìòùáéíóúýâêîôûãñõäëïöü¡¿çãñõ'\-]+\s?)+)$@";
  $result = preg_match($regex, $name, $matches);
  if (!preg_match($regex, $name, $matches)) {
    // Split name without middle_name in first name and last name(s).
    $regex = "@^(?'title'(\b(" . $titles_regex . ")\b\.?\s)?)(?'first_name'(([A-ZÀÈÌÒÙÁÉÍÓÚÝÂÊÎÔÛÃÑÕÄËÏÖÜŸÇÅa-zàèìòùáéíóúýâêîôûãñõäëïöü¡¿çãñõ'\-\.]+)\s*)+?)(?'last_name'(\b[A-ZÀÈÌÒÙÁÉÍÓÚÝÂÊÎÔÛÃÑÕÄËÏÖÜŸÇÅa-zàèìòùáéíóúýâêîôûãñõäëïöü¡¿çãñõ'][a-zàèìòùáéíóúýâêîôûãñõäëïöü¡¿çãñõ'\-]+\s?)+)$@";
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
  $name = trim(preg_replace('!\s+!', ' ', $name));
  if (!empty($name)) return $name;
}

function spcivipush_valid_first_name($first_name) {
  $first_name = trim(preg_replace('!\s+!', ' ', $first_name));
  if (!empty($first_name)) return $first_name;
}

function spcivipush_valid_middle_name($middle_name) {
  $middle_name = trim(preg_replace('!\s+!', ' ', $middle_name));
  if (!empty($middle_name)) return $middle_name;
}

function spcivipush_valid_last_name($last_name) {
  $last_name = trim(preg_replace('!\s+!', ' ', $last_name));
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
  $regex = "@^[0-9]+.*$@";
  if (preg_match($regex, $house_number)) {
    return $house_number;
  }
}

function spcivipush_valid_house_number_addition($house_number_addition) {
  $house_number_addition = trim($house_number_addition);
  if (isset($house_number_addition) && strlen($house_number_addition) > 0) return $house_number_addition;
}

function spcivipush_valid_street_and_number($street_and_number) {
  $street_and_number = trim($street_and_number);
  $street_and_number = ucfirst($street_and_number);
  if (!empty($street_and_number)) return $street_and_number;
}

function spcivipush_valid_postal_code($postal_code) {
  $postal_code = trim($postal_code);
  if (preg_match('@^([1-9][0-9]{3})\s?([a-zA-Z]{2})$@', $postal_code, $matches)) {
    return $matches[1] . ' ' . strtoupper($matches[2]);
  }
}

function spcivipush_valid_locality($locality) {
  $locality = trim($locality);
  $locality = ucfirst($locality);
  if (!empty($locality)) return $locality;
}

function spcivipush_valid_notes($notes) {
  return $notes;
}

function spcivipush_valid_overwrite($overwrite) {
  $overwrite = empty($overwrite) ? FALSE : TRUE;
  return $overwrite;
}

function spcivipush_valid_author($author) {
  return trim($author);
}

function spcivipush_valid_source($source) {
  return trim($source);
}

function spcivipush_valid_groups($groups) {
  return $groups;
}


/*
 * Longer middle names first!
 */
function spcivipush_middle_names() {
  return array("Uijt te de","van van de","uijt te de","Voor in ‘t","De van der","de van der","Van van de","voor in ‘t","Uit te de","onder den","Voor in t","onder het","van de l’","De die le","de die le","Van de l’","voor in t","Onder het","Onder den","uit te de","over het","uijt den","Onder de","boven d’","uijt ten","Uijt ten","over den","Over den","onder de","onder ‘t","Onder ‘t","Van de l","Boven d’","van de l","voor den","Over het","Uijt den","Voor den","Over ‘t","Boven d","Von dem","boven d","Von der","von den","Von den","von der","von dem","Onder t","van der","Van ter","Van gen","van het","van gen","Van het","van ter","over ‘t","van den","Van den","Van Der","Van der","onder t","Van Den","bij den","auf dem","Auf den","Auf dem","voor de","uit het","Uit het","auf den","Auf der","Voor de","voor ‘t","auf ter","Auf ter","auf der","aan het","Aan het","uijt de","vor der","Uijt de","uijt ‘t","Uijt ‘t","Vor der","Uit ten","Aan der","aan der","aan den","Aan den","uit ten","uit den","Uit den","aus der","Bij het","Bij den","Voor ‘t","over de","Over de","bij het","Aus dem","aus dem","Aus den","Aus der","aus den","in het","die le","In der","in der","In het","Uit de","van ‘t","Van de","van De","van de","Van ‘t","Uit ‘t","In den","uit de","uit ‘t","in den","Op ten","op het","Over t","Op het","Die le","op gen","over t","op ten","Von ‘t","Bij ‘t","aus ‘m","bij ‘t","Bij de","von ‘t","bij de","Op gen","op der","Aan ‘t","aan ‘t","De las","de las","Van la","van la","Aan de","aan de","op den","Op der","De die","de die","Op den","Aus ‘m","Onder","op ‘t","Op ‘t","Op de","op de","onder","Uit t","Van t","De l’","uit t","Dalla","de l’","De la","de le","De le","de la","Von t","bij t","Am de","aan t","Aan t","am de","Aus m","Bij t","von t","aus m","Degli","dalla","van t","In de","in ‘t","In ‘t","della","in de","unter","Unter","Della","degli","deca","uijt","Over","over","Voor","in t","In t","Uijt","voor","op t","De l","thoe","de l","thor","Deca","Thor","Dal’","Thoe","Op t","dal’","zum","Zur","toe","zur","tot","Toe","Tot","uit","vom","Von","Vom","ver","Ver","van","Van","vor","Vor","Uit","von","Zum","Los","Den","den","del","Del","Dei","dei","Der","der","dos","Het","Dos","don","Des","des","das","Das","Aus","aus","auf","Auf","Aan","aan","Ben","ben","Dal","dal","bin","Bin","Bij","bij","het","Don","Ter","las","ter","ten","Ten","los","les","Les","Tho","Las","tho","d’","‘T","‘t","D’","Da","da","l’","op","S’","‘s","s’","te","af","am","Am","al","Af","de","Al","zu","Zu","to","Te","De","du","El","Du","Le","do","Op","el","la","in","L’","In","im","La","Im","Do","To","lo","di","‘S","Of","Lo","le","of","Di","A","l","S","s","L","I","T","t","D","d","i","a");
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

  // Find same contacts based on source (webform).
  if (preg_match('/^Form: .*\(id: [0-9]+, sid: [0-9]+,[^)]+\)$/', $data['source'])) {
    $params = array(
      'option.limit' => 1,
      'contact_is_deleted' => 0,
      'contact_source' => $data['source'],
    );
    $contact_results = $spcivi->api('Contact', 'get', $params);
    if (_spcivipush_check_result($contact_results, 'een contact', 'get', $params, $debug_info)) {
      if (!empty($contact_results['values'])) {
        $contact_result = array_shift($contact_results['values']);
        if (!empty($contact_result['contact_id'])) {
          $contact_id = $contact_result['contact_id'];
          $same_contacts[$contact_id] = $contact_id;
          return $same_contacts;
        }
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
    foreach ($same_contacts_phone as $contact_id) {
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
          if (spcivipush_first_name_equal($data, $contact)) {
            $same_contacts[$contact_id] = $contact_id;
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
    (
      isset($data['first_name']) || 
      isset($data['last_name'])
    )
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
              $contact = array_pop($contact_results['values']);
              if (spcivipush_first_name_equal($data, $contact)) {
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

  // Find similar contacts based on address.
  if (
    isset($data['postal_code']) &&
    isset($data['house_number']) &&
    (
      isset($data['first_name']) || 
      isset($data['last_name'])
    )
  ) {

    // Check if address with same house number and postal code exists.
    $params = array(
      'option.limit' => 5,
      'street_number' => $data['house_number'],
      'postal_code' => $data['postal_code'],
    );
    $get_address_result = $spcivi->api('Address', 'get', $params);
    if(_spcivipush_check_result($get_address_result, 'een adres', 'get', $params, $debug_info)) {
      if (!empty($get_address_result['values'])) {
        foreach ($get_address_result['values'] as $value) {
          $address_contact_ids[$value['contact_id']] = $value['contact_id'];
        }
        foreach ($address_contact_ids as $contact_id) {
          // Get contact information.
          $params = array(
            'contact_id' => $contact_id,
            'contact_is_deleted' => 0,
          );
          $contact_results = $spcivi->api('Contact', 'get', $params);
          if (_spcivipush_check_result($contact_results, 'een contact', 'get', $params, $debug_info)) {
            if (!empty($contact_results['values'])) {
              $contact = array_pop($contact_results['values']);
              if (spcivipush_first_name_equal($data, $contact)) {
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

function spcivipush_add_contact($data, $debug_info) {
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
        'location_type_id' => 1,
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

    // Add notes.
    if (spcivipush_add_notes($contact_id, $data, $debug_info) === FALSE) {
      return FALSE;
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
    watchdog('spcivipush', '<p>Fout bij het ' . $action_name . ' van ' . $dataname . ' ' . $place_name . ' CiviCRM.</p>Resultaat:<pre>' . print_r($result, TRUE) . '</pre>Parameters:<pre>' . print_r($params, TRUE) . '</pre>Debug info:<pre>' . print_r($debug_info, TRUE) . '</pre>', array(), WATCHDOG_ERROR);
    return FALSE;
  }
  return TRUE;
}

function spcivipush_check_address_manually($street_and_number, $contact_id) {
  // Check if there is a house number numeric addition in a street_address, if so, send mail for manual correction.
  if (!empty($street_and_number)) {
    if (preg_match('/ \d+[^0-9]+\d+/', $street_and_number)) {
      // Addition found, send mail.
      $check_address_email = variable_get('spcivipush_check_address_email_address', '');
      if (!empty($check_address_email)) {
        // Send mail for manually merging.
        $params = array(
          'type' => 'house_number_addition_check',
          'description' => 'controle huisnummer toevoeging',
          'contact_id' => $contact_id,
          'street_and_number' => $street_and_number,
        );
        spcivipush_send_address_check_mail($params);
      }
    }
  }
}

function spcivipush_merge_contacts($data, $same_contacts, $debug_info) {
  if (!empty($data['overwrite']) && !empty($data['contact_id']) && spcivipush_overwrite_location()) {
    $overwrite = TRUE;
  }
  else {
    $overwrite = FALSE;
  }
  $merged_data = array();
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

      // check if new name data.
      if (!empty($data['first_name']) || !empty($data['last_name']) || !empty($data['name'])) {

        // Check if name has changed.
        $new_first = empty($data['first_name']) ? '' : $data['first_name'];
        $new_middle = empty($data['middle_name']) ? '' : $data['middle_name'];
        $new_last = empty($data['last_name']) ? '' : $data['last_name'];
        $old_name = empty($contact['display_name']) ? '' : $contact['display_name'];
        $old_name = trim(preg_replace('!\s+!', ' ', $old_name));
        $new_name = trim(preg_replace('!\s+!', ' ', implode(' ', array($new_first, $new_middle, $new_last))));
        $new_name = !empty($new_name) ? $new_name : $data['name'];
        $new_name = !empty($new_name) ? $new_name : '';
        
        if (!empty($new_name) && $old_name !== $new_name) {
          if (empty($old_name) || $overwrite) {
            // Write new name without confirmation.

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
              $merged_data['name'] = array(
                'original' => $contact['display_name'],
                'new' => $new_name,
                'changed' => TRUE,
              );
            }
            else {
              return FALSE;
            }
          }
          else {
            // No new name written, create action to check new name.
            $merged_data['name'] = array(
              'original' => $contact['display_name'],
              'new' => $new_name,
              'changed' => FALSE,
            );
          }
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
          if (empty($contact['email']) || $overwrite) {
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
            $merged_data['email'] = array(
              'email' => $data['email'],
              'primary_text' => $primary ? ' primair' : ' (nog niet ingesteld als primair)',
            );
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
          if (empty($contact['phone']) || $overwrite) {
            $primary = 1;
            $location_type_id = 1;
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
            $merged_data['phone'] = array(
              'phone' => $data['telephone'],
              'primary_text' => $primary ? ' primair' : ' (nog niet ingesteld als primair)',
            );
          }
          else {
            return FALSE;
          }
        }
      }

      // Merge address data if no address with same house number or postal code exists.
      if (!empty($data['house_number']) || !empty($data['postal_code']) || (!empty($data['street_and_number']) && !empty($data['locality']))) {
        // Check if address with same house number exists.
        if (!empty($data['house_number'])) {
          $params = array(
            'contact_id' => $contact_id,
            'street_number' => $data['house_number'],
          );
          $get_street_number_address_result = $spcivi->api('Address', 'get', $params);
          if(!_spcivipush_check_result($get_street_number_address_result, 'een adres', 'get', $params, $debug_info)) {
            return FALSE;
          }
        }
        // Check if address with same postal code exists.
        if (!empty($data['postal_code'])) {
          $params = array(
            'contact_id' => $contact_id,
            'postal_code' => $data['postal_code'],
          );
          $get_postal_code_address_result = $spcivi->api('Address', 'get', $params);
          if(!_spcivipush_check_result($get_postal_code_address_result, 'een adres', 'get', $params, $debug_info)) {
            return FALSE;
          }
        }
        // Check if address with same street and number exists.
        if (!empty($data['street_and_number'])) {
          $params = array(
            'contact_id' => $contact_id,
            'street_address' => $data['street_and_number'],
          );
          $get_street_address_result = $spcivi->api('Address', 'get', $params);
          if(!_spcivipush_check_result($get_street_address_result, 'een adres', 'get', $params, $debug_info)) {
            return FALSE;
          }
        }

        if (
            empty($get_street_number_address_result['values']) &&
            empty($get_postal_code_address_result['values']) &&
            empty($get_postal_code_address_result['values'])
        ) {
          if (empty($contact['address_id']) || $overwrite) {
            // Add primary address.
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
          $address_merge_fields = array();
          foreach ($autosync as $key => $civikey) {
            if (!empty($data[$key])) {
              $params[$civikey] = $data[$key];
              $address_merge_fields[$civikey] = $data[$key];
              $address_data_found = TRUE;
            }
          }
          if ($address_data_found) {
            $create_address_results = $spcivi->api('Address', 'create', $params);
            if(_spcivipush_check_result($create_address_results, 'een adres', 'create', $params, $debug_info)) {
              // If new address was added to existing one.
              if (
                !empty($create_address_results['values']) && 
                !empty($contact['address_id'])
                ) {
                foreach ($address_merge_fields as $key => $value) {
                  $merged_data['address'][$key] = $value;
                };
                $merged_data['address']['primary_text'] = $primary ? ' primair' : ' (nog niet ingesteld als primair)';
              }
            }
            else {
              return FALSE;
            }
          }
        }
      }

      // Add notes.
      if (spcivipush_add_notes($contact_id, $data, $debug_info) === FALSE) {
        return FALSE;
      }

      // Create activities.
      if (!empty($merged_data)) {
        $text = '<p>Bron: ' . $data['source'] . '</p>';
        if (!empty($merged_data['name'])) {
          if ($merged_data['name']['changed']) {
            $text .= '<p>De naam van het contact is gewijzigd van "' . $merged_data['name']['original'] . '" naar "' . check_plain($merged_data['name']['new']) . '".</p>';
          }
          else {
            $text .= '<p>Er is een naamwijziging doorgegeven van "' . $merged_data['name']['original'] . '" naar "' . check_plain($merged_data['name']['new']) . '". Deze wijziging is nog niet doorgevoerd.</p>';
          }
        }
        if (
          !empty($merged_data['email']) ||
          !empty($merged_data['phone']) ||
          !empty($merged_data['address'])
        ) {
          $data_added = TRUE;
          $text .= "<p>De volgende gegevens zijn toegevoegd:</p>";
          if (!empty($merged_data['email'])) {
            $text .= '<p>e-mailadres' . $merged_data['email']['primary_text'] . ': ' . check_plain($merged_data['email']['email']) . "</p>";
          }
          if (!empty($merged_data['phone'])) {
            $text .= '<p>telefoonnummer' . $merged_data['phone']['primary_text'] . ': ' . check_plain($merged_data['phone']['phone']) . "</p>";
          }
          if (!empty($merged_data['address'])) {
            $text .= '<p>adres' . $merged_data['address']['primary_text'] . ':<br/>';
            unset($merged_data['address']['primary_text']);
            foreach ($merged_data['address'] as $value) {
              $text .= '- ' . check_plain($value) . "<br/>";
            }
            $text .= '</p>';
          }
        }
        else {
          $data_added = FALSE;
        }
        // Check if spbellen module exists, if so, get caller data.
        $caller_text = spcivipush_get_caller_text($debug_info);
        $text .= $caller_text;
        if ($overwrite || $data_added) {
          spcivipush_create_activity($contact_id, 'Wijziging contact via website', 'Completed', 'Wijziging gegevens contact', $text, $debug_info);
        }
        spcivipush_create_activity($contact_id, 'Controle wijziging contact via website', 'Scheduled', 'Contact gegevens zijn gewijzigd', $text, $debug_info);
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
    drupal_set_message('Contact nr. '.$contact_id.' was added to groups ('.$groups_text.').');
    watchdog('spcivipush', 'Contact nr. '.$contact_id.' was added to groups ('.$groups_text.').');
  }
  return TRUE;
}

function spcivipush_first_name_equal($data, $contact) {
  // Check if first name and last name are identical.
  if (
    !empty($contact['first_name']) &&
    !empty($data['first_name']) &&
    strlen($contact['first_name']) > 2 &&
    strlen($data['first_name']) > 2 &&
    (
      strpos($contact['first_name'], $data['first_name']) !== FALSE ||
      strpos($data['first_name'], $contact['first_name']) !== FALSE ||
      (
        !empty($contact['last_name']) &&
        strpos($contact['last_name'], $data['first_name']) !== FALSE
      ) ||
      (
        !empty($data['last_name']) &&
        strpos($data['last_name'], $contact['first_name']) !== FALSE
      )
    )
  ) {
    return TRUE;
  }
  return FALSE;
}

function spcivipush_send_address_check_mail($params) {
  $check_address_email = variable_get('spcivipush_check_address_email_address', '');
  if (!empty($check_address_email)) {
    $message = drupal_mail('spcivipush', 'check_address', $check_address_email, language_default(), $params, 'webmaster@sp.nl');
    if (!empty($message['result'])) { 
      watchdog('spcivipush', 'Contact controle adres ('.$params['description'].'), mail verzonden.');
      drupal_set_message('Contact '.$params['description'].', mail verzonden.');
    }
    else {
      watchdog('spcivipush', 'Contact '.$params['description'].', geen mail verzonden verzonden (fout opgetreden).');
      drupal_set_message('Contact '.$params['description'].', geen mail verzonden (fout opgetreden).');
    }
  }
}

function spcivipush_overwrite_location() {
  $ip_address = ip_address();
  $location_ip = variable_get('spcivipush_overwrite_location_ip', FALSE);                              
  if ($ip_address === $location_ip) {
    return TRUE;
  }
  return FALSE;
}

function spcivipush_create_activity($contact_id, $activity_type, $status, $subject, $text, $debug_info) {
  $spcivi = \SPCivi::getInstance();                                                                   
  set_time_limit(60);
  $params = array( 
    'activity_type_id' => $activity_type,
    'subject' => $subject,
    'target_id' => $contact_id,
    'details' => $text,
    'status_id' => $status,
    'priority_id' => 2,
  );
  $activity_results = $spcivi->api('Activity', 'create', $params);
  if (_spcivipush_check_result($activity_results, 'een activiteit', 'create', $params, $debug_info)) {     
    if (!empty($activity_results['values'])) {
      return TRUE;                                                                        
    }                                                                                               
  }                                                                                                 
  return FALSE;
}


function spcivipush_get_caller_text($debug_info) {
  if (is_object($debug_info)) {
    if (isset($debug_info->sid)) {
      if (!empty($debug_info->sid)) {
        if (db_table_exists('spbellen_action') && db_table_exists('spbellen_caller')) {
          $caller_text = '';
          $sid = $debug_info->sid;
          $sql = 'SELECT * FROM {spbellen}_action ac INNER JOIN {spbellen_caller} ca ON ac.caller_id = ca.caller_id INNER JOIN users us ON ca.caller_id = us.uid WHERE webform_submission_id = :sid';
          $query = db_query($sql, array(':sid' => $sid)); 
          $result = $query->fetchAssoc();
          if (!empty($result)) {
            $caller_text = '';
            $caller_text .= '<p>Gegevens beller:<br/>';
            $caller_text .= 'Naam: ' . check_plain($result['display_name']) . '<br/>';
            $caller_text .= 'Email: ' . check_plain($result['mail']) . '<br/>';
            $caller_text .= 'Telefoon: ' . check_plain($result['phone']) . '<br/>';
            $caller_text .= 'Afdeling: ' . check_plain($result['afdeling']) . '<br/>';
            $caller_text .= '</p>';
          }
          return $caller_text;
        }
      }
    }
  }
}


function spcivipush_add_notes($contact_id, $data, $debug_info) {
  // If available add note.
  if (!empty($data['notes'])) {

    $spcivi = \SPCivi::getInstance();
    set_time_limit(60);

    // Get field ids.
    $content_field_id = variable_get('spcivipush_note_content_field');
    $use_field_id = variable_get('spcivipush_note_use_field');
    $author_field_id = variable_get('spcivipush_note_author_field');
    $date_field_id = variable_get('spcivipush_note_date_field');
    $source_field_id = variable_get('spcivipush_note_source_field');

    // Get existing custom fields.
    $params = array(
      'sequential' => 1,
      'entity_id' => $contact_id,
    );
    $custom_fields_result = $spcivi->api('CustomValue', 'get', $params);
    if (!_spcivipush_check_result($custom_fields_result, 'velden', 'get', $params, $debug_info)) {
      return FALSE;
    }
    foreach ($custom_fields_result['values'] as $field_values) {
      if ($field_values['id'] === $source_field_id) {
        foreach ($field_values as $key => $value) {
          if( filter_var($key, FILTER_VALIDATE_INT) ){
            if ($value === $data['source']) {
              // Note was already added.
              watchdog('spcivipush', 'Notitie bestaat reeds.');
              return;
            }
          }
        }
      }
    }

    $date = format_date(time(), 'custom', 'Ymd', date_default_timezone_get());

    foreach ($data['notes'] as $note_data) {
      if (!empty(trim($note_data['note_content']))) {
        $params = array(
          'contact_id' => $contact_id,
          'entity_id' => $contact_id,
          'custom_' . $content_field_id => $note_data['note_content'],
          'custom_' . $use_field_id => $note_data['note_use'],
          'custom_' . $author_field_id => $data['author'],
          'custom_' . $date_field_id => $date,
          'custom_' . $source_field_id => $data['source'],
        );
        $create_note_result = $spcivi->api('CustomValue', 'create', $params);
        if (!_spcivipush_check_result($create_note_result, 'een notitie', 'create', $params, $debug_info)) {
          return FALSE;
        }
        watchdog('spcivipush', 'Notitie toegevoegd.');
        drupal_set_message('Notitie toegevoegd.');
      }
    }
  }
}
