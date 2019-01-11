<?php

/**
 * Helper functions for module.
 */

/**
 * Sync contact data.
 */
function spcivipush_sync_contact($data, $debug_info) {
  watchdog('spcivipush', 'Received data:<pre>' . print_r($data, TRUE) . '</pre>');
  // Find similar existing contacts in civi.
  $existing_contact = spcivipush_find_existing_contact($data, $debug_info);
  if ($existing_contact !== FALSE) {
    // Sync contact.
    if (empty($existing_contact)) {
      if (!empty($data['contact_id'])) {
        watchdog('spcivipush', 'Contact nr. ' . $data['contact_id'] . ' kon niet worden gevonden in CiviCRM.');
        drupal_set_message('Contact nr. ' . $data['contact_id'] . ' kon niet worden gevonden in CiviCRM.');
        return 'contact id not found';
      }
      else {
        $contact_id = spcivipush_add_contact($data, $debug_info);
      }
    }
    else {
      $contact_id = spcivipush_merge_contacts($data, $existing_contact, $debug_info);
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

/**
 * Finds same contacts.
 */
function spcivipush_find_existing_contact($data, $debug_info) {
  $spcivi = \SPCivi::getInstance();
  set_time_limit(60);
  $existing_contact = array();

  // Find existing contact based on contact_id.
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
        $existing_contact = array_pop($contact_results['values']);
        return $existing_contact;
      }
    }
    else {
      return FALSE;
    }
  }

  // Find existing contact based on source (webform).
  if (preg_match('/^Form: .*\(id: [0-9]+, sid: [0-9]+,[^)]+\)$/', $data['source'])) {
    $params = array(
      'option.limit' => 1,
      'contact_is_deleted' => 0,
      'contact_source' => $data['source'],
      'contact_is_deleted' => 0,
    );
    $contact_results = $spcivi->api('Contact', 'get', $params);
    if (_spcivipush_check_result($contact_results, 'een contact', 'get', $params, $debug_info)) {
      if (!empty($contact_results['values'])) {
        $existing_contact = array_pop($contact_results['values']);
        return $existing_contact;
      }
    }
    else {
      return FALSE;
    }
  }

  // Find same contacts based on mobile phone.
  if (
    isset($data['telephone']) &&
    strpos($data['telephone'], '06') === 0 &&
    strlen($data['telephone']) === 10 &&
    (
      isset($data['last_name']) ||
      isset($data['first_name'])
    )
  ) {
    $mobile_phone = $data['telephone'];
    $numbers_to_check = array(
      $mobile_phone,
      '0031' . substr($mobile_phone, 1),
      '31' . substr($mobile_phone, 1),
    );
    $params = array(
      'option.limit' => 5,
      'phone_numeric' => array('IN' => $numbers_to_check),
      'api.Contact.get' => array(),
    );
    $mobile_phone_results = $spcivi->api('Phone', 'get', $params);
    if (_spcivipush_check_result($mobile_phone_results, 'telefoonnummers', 'get', $params, $debug_info)) {
      if (!empty($mobile_phone_results['values'])) {
        foreach ($mobile_phone_results['values'] as $value) {
          if (!empty($value['api.Contact.get']['values'])) {
            foreach ($value['api.Contact.get']['values'] as $existing_contact) {
              if (empty($existing_contact['contact_is_deleted'])) {
                if (spcivipush_extra_check($data, $existing_contact)) {
                  return $existing_contact;
                }
              }
            }
          }
        }
      }
    }
    else {
      return FALSE;
    }
  }

  // Find similar contacts based on email, first- and last name.
  if (isset($data['email'])) {
    $params = array(
      'option.limit' => 5,
      'email' => $data['email'],
      'api.Contact.get' => array(),
    );
    $email_results = $spcivi->api('Email', 'get', $params);
    if (_spcivipush_check_result($email_results, 'e-mailadressen', 'get', $params, $debug_info)) {
      if (!empty($email_results['values'])) {
        foreach ($email_results['values'] as $value) {
          if (!empty($value['api.Contact.get']['values'])) {
            foreach ($value['api.Contact.get']['values'] as $existing_contact) {
              if (empty($existing_contact['contact_is_deleted'])) {
                if (spcivipush_extra_check($data, $existing_contact)) {
                  return $existing_contact;
                }
              }
            }
          }
        }
      }
    }
    else {
      return FALSE;
    }
  }

  // Find similar contacts based on address.
  $params = array();
  if (
      isset($data['first_name']) ||
      isset($data['last_name'])
  ) {
    if (isset($data['postal_code']) && isset($data['house_number'])) {
      $params = array(
        'option.limit' => 5,
        'street_number' => $data['house_number'],
        'postal_code' => $data['postal_code'],
        'api.Contact.get' => array(),
      );
    }
    if (isset($data['street']) && isset($data['house_number']) && isset($data['locality'])) {
      $params = array(
        'option.limit' => 5,
        'street_number' => $data['house_number'],
        'street_name' => $data['street'],
        'city' => $data['locality'],
        'api.Contact.get' => array(),
      );
    }

      if (!empty($params)) {

      // Check if address with same house number and postal code exists.
      $get_address_result = $spcivi->api('Address', 'get', $params);
      if(_spcivipush_check_result($get_address_result, 'een adres', 'get', $params, $debug_info)) {
        if (!empty($get_address_result['values'])) {
          foreach ($get_address_result['values'] as $value) {
            if (!empty($value['api.Contact.get']['values'])) {
              foreach ($value['api.Contact.get']['values'] as $existing_contact) {
                if (empty($existing_contact['contact_is_deleted'])) {
                  if (spcivipush_extra_check($data, $existing_contact)) {
                    return $existing_contact;
                  }
                }
              }
            }
          }
        }
      }
      else {
        return FALSE;
      }
    }
  }

  return array();
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
    'birth_date' => 'birth_date',
    'gender' => 'gender_id',
    'initials' => 'custom_111',
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
    $check_name = TRUE;
  }
  unset($sync_items['name']);

  // If available, add phone.
  $phone_params = array();
  if (!empty($data['telephone']) && (empty($data['phone_mobile']) || $data['telephone'] !== $data['phone_mobile'])) {
    if (empty($data['phone_mobile'])) {
      $is_primary = 0;
    }
    else {
      $is_primary = 1;
    }
    if (strpos($data['telephone'], '06') === 0) {
      $phone_type_id = 2;
    }
    else {
      $phone_type_id = 1;
    }
    $phone_params[] = array(
      'location_type_id' => 1,
      'phone' => $data['telephone'],
      'is_primary' => $is_primary,
      'phone_type_id' => $phone_type_id,
    );
    $params = array_merge($params, array('api.Phone.create' => $phone_params));
  }

  // If available, add moble phone.
  if (!empty($data['phone_mobile'])) {
    $phone_type_id = 2;
    $phone_params[] = array(
      'location_type_id' => 1,
      'phone' => $data['phone_mobile'],
      'is_primary' => 1,
      'phone_type_id' => $phone_type_id,
    );
    $params = array_merge($params, array('api.Phone.create' => $phone_params));
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
  $address_params = array(
    'location_type_id' => 1,
    'is_primary' => 1,
  );

  // Get country ID.
  if (!empty($data['country'])) {
    $country_id = array_search($data['country'], spcivipush_get_country_ids());
  }

  if (empty($country_id)) {
    $address_params['country_id'] = 1152;
  }
  else {
    $address_params['country_id'] = $country_id;
  }
  $address_data_found = FALSE;
  foreach ($autosync as $key => $civikey) {
    if (!empty($data[$key])) {
      $address_params[$civikey] = $data[$key];
      $address_data_found = TRUE;
    }
  }
  if ($address_data_found) {
    $params = array_merge($params, array('api.Address.create' => $address_params));
  }

  // Add notes.
  if (spcivipush_add_notes(NULL, $data, $params, $debug_info) === FALSE) {
    return FALSE;
  }

  // Add unverified tag.
  $tag_id = variable_get('spcivipush_unverified_tag', FALSE);
  if (!empty($tag_id)) {
    $tag_params = array(
      'tag_id' => $tag_id,
    );
    $params = array_merge($params, array('api.EntityTag.create' => $tag_params));
  }

  // Create contact.
  $create_contact_results = $spcivi->api('Contact', 'create', $params);
  if (_spcivipush_check_result($create_contact_results, 'een contact', 'create', $params, $debug_info)) {
    $contact_id = $create_contact_results['id'];
    $contact = array_pop($create_contact_results['values']);
    drupal_set_message('Contact in CiviCRM toegevoegd: ' . $contact_id);
    watchdog('spcivipush', 'Contact in CiviCRM toegevoegd: ' . $contact_id);

    // If no split name, send mail for manually checking name.
    if (!empty($check_name)) {
      $text = 'Te corrigeren naam: "' . $data['name'] . '".';
      spcivipush_create_activity($contact_id, 'Controle wijziging contact via website', 'Scheduled', 'Handmatig controleren naam nieuw contact', $text, $debug_info);
    }

    return $contact_id;
  }
  return FALSE;
}

function spcivipush_merge_contacts($data, $existing_contact, $debug_info) {
  if (!empty($data['overwrite']) && !empty($data['contact_id'])) {
    $overwrite = TRUE;
  }
  else {
    $overwrite = FALSE;
  }
  $merged_data = array();
  $spcivi = \SPCivi::getInstance();
  set_time_limit(60);

  // Get contact.
  $contact_id = $existing_contact['id'];
  $params = array(
    'id' => $contact_id,
    'api.Email.get' => array(),
    'api.Phone.get' => array(),
    'api.Address.get' => array(),
    'api.CustomValue.get' => array(),
    'return' => 'id,display_name,email,initials,birth_date,gender_id,phone,street_address,house_number,address_id,custom_111',
  );
  $get_contact_results = $spcivi->api('Contact', 'get', $params);
  if (_spcivipush_check_result($get_contact_results, 'contacten', 'get', $params, $debug_info)) {
    if (!empty($get_contact_results['values'])) {
      $full_existing_contact = $get_contact_results['values'][0];

      drupal_set_message('Bestaand contact gevonden: <a href="https://spnet.nl/civicrm/contact/view?reset=1&cid=' . $contact_id . '" target="_blank">' . $contact_id . '</a>');
      watchdog('spcivipush', 'Bestaand contact gevonden: ' . $contact_id);

      // Merge contact.
      $merge_params = array(
        'contact_id' => $contact_id,
      );

      // Merge name data with contact.
      spcivipush_merge_name($full_existing_contact, $data, $overwrite, $merge_params, $merged_data, $debug_info);

      // Merge initials data with contact.
      spcivipush_merge_initials($full_existing_contact, $data, $overwrite, $merge_params, $merged_data, $debug_info);

      // Merge email data with contact.
      spcivipush_merge_email($full_existing_contact, $data, $overwrite, $merge_params, $merged_data, $debug_info);

      // Merge birth date data with contact.
      spcivipush_merge_birth_date($full_existing_contact, $data, $overwrite, $merge_params, $merged_data, $debug_info);

      // Merge gender data with contact.
      spcivipush_merge_gender($full_existing_contact, $data, $overwrite, $merge_params, $merged_data, $debug_info);

      // Merge phone data with contact.
      spcivipush_merge_telephone($full_existing_contact, $data, $overwrite, $merge_params, $merged_data, $debug_info);

      // Merge address data.
      spcivipush_merge_address($full_existing_contact, $data, $overwrite, $merge_params, $merged_data, $debug_info);

      // Add notes.
      spcivipush_add_notes($full_existing_contact, $data, $merge_params, $debug_info);

      // Merge contact.
      $create_contact_results = $spcivi->api('Contact', 'create', $merge_params);
      if (_spcivipush_check_result($create_contact_results, 'contacten', 'create', $merge_params, $debug_info)) {
        if (!empty($create_contact_results['values'])) {

          // Create activities.
          spcivipush_merge_create_activities($full_existing_contact, $data, $overwrite, $merged_data, $debug_info);

          // Feedback.
          if (!empty($create_contact_results['values'][0]['api.CustomValue.create'])) {
            foreach ($create_contact_results['values'][0]['api.CustomValue.create'] as $cust_value_result) {
              if (isset($cust_value_result['is_error']) && $cust_value_result['is_error'] == 0) {
                watchdog('spcivipush', 'Notitie toegevoegd.');
                drupal_set_message('Notitie toegevoegd.');
              }
            }
          }

          return $contact_id;
        }
      }
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

/**
 * Parses submission data with sync info in raw data array to sync.
 */
function spcivipush_parse_data($data) {
  $validated_data = array();
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

  // If possible, split street and number.
  if (!empty($validated_data['street_and_number'])) {
    $split_street_and_number = spcivipush_split_street_and_number($validated_data['street_and_number']);
    if (!empty($split_street_and_number)) {
      if (empty($validated_data['street']) && !empty($split_street_and_number[1])) {
        $validated_data['street'] = $split_street_and_number[1];
      }
      if (empty($validated_data['house_number']) && !empty($split_street_and_number[2])) {
        $validated_data['house_number'] = $split_street_and_number[2];
      }
      if (empty($validated_data['house_number_addition']) && !empty($split_street_and_number[3])) {
        $validated_data['house_number_addition'] = $split_street_and_number[3];
      }
    }
  }

  // Set street and number if not set.
  if (
    empty($validated_data['street_and_number']) &&
    !empty($validated_data['street']) &&
    !empty($validated_data['house_number'])
  ) {
    $validated_data['street_and_number'] = $validated_data['street'] . ' ' . $validated_data['house_number'];
    if (!empty($validated_data['house_number_addition'])) {
      $validated_data['street_and_number'] = $validated_data['street_and_number'] . ' ' . $validated_data['house_number_addition'];
    }
  }

  // Remove too big house_numbers.
  if (isset($validated_data['house_number'])) {
    if (strlen($validated_data['house_number']) > 5) {
      unset($validated_data['house_number']);
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
 * Checks if the address data is sufficient
 */
function spcivipush_check_address_sufficient($data) {
  // Check for: Testlaan 1 (street_and_number), Amsterdam (locality)
  if (!empty($data['street_and_number']) && !empty($data['locality'])) {
    return TRUE;
  }

  // Check for Testlaan (street) 1 (house_number), Amsterdam (locality)
  if (!empty($data['street']) && !empty($data['house_number']) && !empty($data['locality'])) {
    return TRUE;
  }
  // Check for house number and postal code, for example: 1234AX 7
  // This is sufficient because we can determine the street and city
  // from the postal code.
  if (!empty($data['house_number']) && !empty($data['postal_code'])) {
    return TRUE;
  }
  return FALSE;
}

/**
 * Checks wether a submission contains sufficient data to create new or sync existing civi contact.
 */
function spcivipush_check_submission_sufficient($data) {
  // Check if values are sufficient to create new or sync existing civi contact.
  if (!empty($data['contact_id'])) {
    return TRUE;
  }

  if (
    !empty($data['first_name']) ||
    !empty($data['last_name'])||
    !empty($data['name']) ||
    !empty($data['email'])
  ) {
    if (!empty($data['email']) || !empty($data['telephone']) || spcivipush_check_address_sufficient($data) || !empty($data['birth_date'])) {
      return TRUE;
    }
  }
  watchdog('spcivipush', '<p>Contact kon niet worden aangemaakt, te weinig data.</p><pre>' . print_r($data, TRUE) . '</pre>');
  drupal_set_message('Contact kan niet worden gemaakt!', 'error');
  return FALSE;
}

/**
 * Gets data names.
 */
function spcivipush_get_data_names() {
  $data_names = array(
    'contact_id' => 'Contact id',
    'name' => 'gehele naam',
    'initials' => 'initialen',
    'first_name' => 'voornaam',
    'middle_name' => 'tussenvoegsel',
    'last_name' => 'achternaam',
    'birth_date' => 'geboortedatum',
    'gender' => 'geslacht',
    'email' => 'e-mail',
    'telephone' => 'telefoon',
    'phone_mobile' => 'mobiele telefoon',
    'street' => 'straat',
    'house_number' => 'huisnummer (inc. toevoeging)',
    'house_number_addition' => 'huisnummer toevoeging',
    'street_and_number' => 'straat en huisnummer',
    'postal_code' => 'postcode',
    'locality' => 'plaats',
    'country' => 'land',
    'notes' => 'notities',
    'overwrite' => 'overschrijven',
    'author' => 'auteur',
    'remote_addr' => 'ip addres auteur',
    'source' => 'bron',
    'groups' => 'groepen',
  );
  return $data_names;
}

/**
 * Split house number.
 */
function spcivipush_split_house_number($house_number) {
  $house_number = trim($house_number);
  $regex = "@^([0-9]+)([^0-9].*)$@";
  if (preg_match($regex, $house_number, $matches)) {
    $matches[1] = spcivipush_valid_house_number($matches[1]);
    $matches[2] = spcivipush_valid_house_number_addition($matches[2]);
    if (!empty($matches[2])) {
      return $matches;
    }
  }
  return FALSE;
}

/**
 * Split street and number.
 */
function spcivipush_split_street_and_number($street_and_number) {
  $matches = array();
  // Street and number and addition.
  $regex = "@^(.+)((?<![0-9])[0-9]+)([^0-9].*)$@";
  if (!preg_match($regex, $street_and_number, $matches)) {
    // Only street and number.
    $regex = "@^(.+)((?<![0-9])[0-9]+)$@";
    preg_match($regex, $street_and_number, $matches);
  }
  $matches[1] = spcivipush_valid_street($matches[1]);
  $matches[2] = spcivipush_valid_house_number($matches[2]);
  if (isset($matches[3])) {
    $matches[3] = spcivipush_valid_house_number_addition($matches[3]);
  }
  if (!empty($matches[2])) {
    return $matches;
  }
  return FALSE;
}

/**
 * Split name.
 */
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
    if (strpos($name, ' ' . $orig . ' ') !== FALSE) {
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
    // Split name on middle name, last name first.
    $regex = "@^(?'title'(\b(" . $titles_regex . ")\b\.?\s)?)(?'last_name'(\b[A-ZÀÈÌÒÙÁÉÍÓÚÝÂÊÎÔÛÃÑÕÄËÏÖÜŸÇÅa-zàèìòùáéíóúýâêîôûãñõäëïöü¡¿çãñõ'][A-ZÀÈÌÒÙÁÉÍÓÚÝÂÊÎÔÛÃÑÕÄËÏÖÜŸÇÅa-zàèìòùáéíóúýâêîôûãñõäëïöü¡¿çãñõ'\-]+\s?)+),?\s(?'first_name'(([A-ZÀÈÌÒÙÁÉÍÓÚÝÂÊÎÔÛÃÑÕÄËÏÖÜŸÇÅa-zàèìòùáéíóúýâêîôûãñõäëïöü¡¿çãñõ'\-\.]+)\s*)+?)(?'middle_name'(\b(" . $middle_names_regex . ")\b\s*))$@";
    $result = preg_match($regex, $name, $matches);
    if (!preg_match($regex, $name, $matches)) {
      // Split name without middle_name in first name and last name(s).
      $regex = "@^(?'title'(\b(" . $titles_regex . ")\b\.?\s)?)(?'first_name'(([A-ZÀÈÌÒÙÁÉÍÓÚÝÂÊÎÔÛÃÑÕÄËÏÖÜŸÇÅa-zàèìòùáéíóúýâêîôûãñõäëïöü¡¿çãñõ'\-\.]+)\s*)+?)(?'last_name'(\b[A-ZÀÈÌÒÙÁÉÍÓÚÝÂÊÎÔÛÃÑÕÄËÏÖÜŸÇÅa-zàèìòùáéíóúýâêîôûãñõäëïöü¡¿çãñõ'][A-ZÀÈÌÒÙÁÉÍÓÚÝÂÊÎÔÛÃÑÕÄËÏÖÜŸÇÅa-zàèìòùáéíóúýâêîôûãñõäëïöü¡¿çãñõ'\-]+\s?)+)$@";
      $result = preg_match($regex, $name, $matches);
      if (!preg_match($regex, $name, $matches)) {
        // Split name in first name and last name, last name first.
        $regex = "@^(?'title'(\b(" . $titles_regex . ")\b\.?\s)?)(?'last_name'(\b[A-ZÀÈÌÒÙÁÉÍÓÚÝÂÊÎÔÛÃÑÕÄËÏÖÜŸÇÅa-zàèìòùáéíóúýâêîôûãñõäëïöü¡¿çãñõ'][A-ZÀÈÌÒÙÁÉÍÓÚÝÂÊÎÔÛÃÑÕÄËÏÖÜŸÇÅa-zàèìòùáéíóúýâêîôûãñõäëïöü¡¿çãñõ'\-]+\s?)+),?\s+(?'first_name'((([A-ZÀÈÌÒÙÁÉÍÓÚÝÂÊÎÔÛÃÑÕÄËÏÖÜŸÇÅa-zàèìòùáéíóúýâêîôûãñõäëïöü¡¿çãñõ']\.)+)\s*)+?)$@";
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
    }
  }
  // Set first character uppercase.
  $parts = array(
    'first_name',
    'last_name',
  );
  foreach ($parts as $part) {
    if (isset($matches[$part])) {
      $matches[$part] = trim($matches[$part]);
      if (!empty($matches[$part])) {
        $matches[$part] = ucfirst($matches[$part]);
      }
    }
  }
  return $matches;
}

/**
 * Check if contact id is valid.
 */
function spcivipush_valid_contact_id($contact_id) {
  $contact_id = trim($contact_id);
  $contact_id = preg_replace('/[^0-9]/s', '', $contact_id);
  if (!empty($contact_id)) {
    if (is_numeric($contact_id) && intval($contact_id) == $contact_id && $contact_id > 0) {
      return $contact_id;
    }
  }
  return FALSE;
}

/**
 * Check if name is valid.
 */
function spcivipush_valid_name($name) {
  $name = trim(preg_replace('!\s+!', ' ', $name));
  if (!empty($name)) {
    return $name;
  }
}

/**
 * Check if first name is valid.
 */
function spcivipush_valid_first_name($first_name) {
  $first_name = trim(preg_replace('!\s+!', ' ', $first_name));
  if (!empty($first_name)) {
    return $first_name;
  }
}

/**
 * Check if middle name is valid.
 */
function spcivipush_valid_middle_name($middle_name) {
  $middle_name = trim(preg_replace('!\s+!', ' ', $middle_name));
  if (!empty($middle_name)) {
    return $middle_name;
  }
}

/**
 * Check if last name is valid.
 */
function spcivipush_valid_last_name($last_name) {
  $last_name = trim(preg_replace('!\s+!', ' ', $last_name));
  if (!empty($last_name)) {
    return $last_name;
  }
}

/**
 * Check if initials are valid.
 */
function spcivipush_valid_initials($initials) {
  $initials = strtoupper(preg_replace('![^A-Z]!', '', $initials));
  if (!empty($initials)) {
    $initials = implode('.', str_split($initials));
    return $initials;
  }
}

/**
 * Check if email is valid.
 */
function spcivipush_valid_email($email) {
  $email = trim($email);
  if (valid_email_address($email)) {
    return strtolower($email);
  }
}

/**
 * Check if phone is valid.
 */
function spcivipush_valid_telephone($telephone) {
  if (!empty($telephone)) {
    // Remove non numerical characters.
    $telephone = preg_replace('@[^0-9]@', '', $telephone);

    // 612345678 => 012345678
    if (strlen($telephone) === 9) {
      $telephone = '0' . $telephone;
    }
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

/**
 * Check if phone is valid.
 */
function spcivipush_valid_phone_mobile($phone_mobile) {
  if (!empty($phone_mobile)) {
    // Remove non numerical characters.
    $phone_mobile = preg_replace('@[^0-9]@', '', $phone_mobile);

    // 612345678 => 012345678
    if (strlen($phone_mobile) === 9) {
      $phone_mobile = '0' . $phone_mobile;
    }
    // +31612345678 => 0649492174
    if (
      strlen($phone_mobile) === 11 &&
      strpos($phone_mobile, '31') === 0
    ) {
      $phone_mobile = substr_replace($phone_mobile, '0', 0, 2);
    }
    // 0031612345678 => 0649492174
    if (
      strlen($phone_mobile) === 13 &&
      strpos($phone_mobile, '0031') === 0
    ) {
      $phone_mobile = substr_replace($phone_mobile, '0', 0, 4);
    }
    return $phone_mobile;
  }
}

/**
 * Check if street is valid.
 */
function spcivipush_valid_street($street) {
  $street = trim($street);
  $street = ucfirst($street);
  if (!empty($street)) {
    return $street;
  }
}

/**
 * Check if house number is valid.
 */
function spcivipush_valid_house_number($house_number) {
  // Allow house number and addition, split when parsing data.
  $house_number = trim($house_number);
  $regex = "@^[0-9]{1,5}([^0-9].*)?$@";
  if (preg_match($regex, $house_number)) {
    return $house_number;
  }
}

/**
 * Check if house number addition is valid.
 */
function spcivipush_valid_house_number_addition($house_number_addition) {
  $house_number_addition = trim($house_number_addition);
  if (isset($house_number_addition) && strlen($house_number_addition) > 0 && strlen($house_number_addition) <= 16) {
    return $house_number_addition;
  }
}

/**
 * Check if street and number is valid.
 */
function spcivipush_valid_street_and_number($street_and_number) {
  $street_and_number = trim($street_and_number);
  $street_and_number = ucfirst($street_and_number);
  if (!empty($street_and_number)) {
    return $street_and_number;
  }
}

/**
 * Check if postal code is valid.
 */
function spcivipush_valid_postal_code($postal_code) {
  $postal_code = trim($postal_code);
  if (!empty($postal_code)) {
    if (preg_match('@^([1-9][0-9]{3})\s?([a-zA-Z]{2}).*$@', $postal_code, $matches)) {
      return $matches[1] . ' ' . strtoupper($matches[2]);
    }
  }
}

/**
 * Check if locality is valid.
 */
function spcivipush_valid_locality($locality) {
  $locality = trim($locality);
  $locality = strtoupper($locality);
  if (!empty($locality)) {
    return $locality;
  }
}

/**
 * Check if country id is valid.
 */
function spcivipush_valid_country($country) {
  $country = strtoupper(trim($country));
  if (preg_match('@^[A-Z]+$@', $country)) {
    return $country;
  }
}

/**
 * Check if birth date is valid.
 */
function spcivipush_valid_birth_date($birth_date) {
  $birth_date = trim($birth_date);
  if (preg_match('@^[1-2][0-9]{3}-[0-9]{2}-[0-9]{2}$@', $birth_date)) {
    return $birth_date;
  }
}

/**
 * Check if gender is valid.
 */
function spcivipush_valid_gender($gender) {
  $gender = trim($gender);
  if (preg_match('@^[mva]$@', $gender)) {
    switch ($gender) {
      case 'v':
      $gender = 1;
      break;

      case 'm':
      $gender = 2;
      break;

      case 'a':
      $gender = 3;
      break;
    }
    return $gender;
  }
}

/**
 * Check if note is valid.
 */
function spcivipush_valid_notes($notes) {
  return $notes;
}

/**
 * Check if overwrite is valid.
 */
function spcivipush_valid_overwrite($overwrite) {
  $overwrite = empty($overwrite) ? FALSE : TRUE;
  return $overwrite;
}

/**
 * Check if author is valid.
 */
function spcivipush_valid_author($author) {
  return trim($author);
}

/**
 * Check if remote address is valid.
 */
function spcivipush_valid_remote_addr($remote_addr) {
  return trim($remote_addr);
}

/**
 * Check if source is valid.
 */
function spcivipush_valid_source($source) {
  return trim($source);
}

/**
 * Check if groups is valid.
 */
function spcivipush_valid_groups($groups) {
  return $groups;
}


/**
 * Longer middle names first.
 */
function spcivipush_middle_names() {
  return array("Uijt te de","van van de","uijt te de","Voor in ‘t","De van der","de van der","Van van de","voor in ‘t","Uit te de","onder den","Voor in t","onder het","van de l’","De die le","de die le","Van de l’","voor in t","Onder het","Onder den","uit te de","over het","uijt den","Onder de","boven d’","uijt ten","Uijt ten","over den","Over den","onder de","onder ‘t","Onder ‘t","Van de l","Boven d’","van de l","voor den","Over het","Uijt den","Voor den","Over ‘t","Boven d","Von dem","boven d","Von der","von den","Von den","von der","von dem","Onder t","van der","Van ter","Van gen","van het","van gen","Van het","van ter","over ‘t","van den","Van den","Van Der","Van der","onder t","Van Den","bij den","auf dem","Auf den","Auf dem","voor de","uit het","Uit het","auf den","Auf der","Voor de","voor ‘t","auf ter","Auf ter","auf der","aan het","Aan het","uijt de","vor der","Uijt de","uijt ‘t","Uijt ‘t","Vor der","Uit ten","Aan der","aan der","aan den","Aan den","uit ten","uit den","Uit den","aus der","Bij het","Bij den","Voor ‘t","over de","Over de","bij het","Aus dem","aus dem","Aus den","Aus der","aus den","in het","die le","In der","in der","In het","Uit de","van ‘t","Van de","van De","van de","Van ‘t","Uit ‘t","In den","uit de","uit ‘t","in den","Op ten","op het","Over t","Op het","Die le","op gen","over t","op ten","Von ‘t","Bij ‘t","aus ‘m","bij ‘t","Bij de","von ‘t","bij de","Op gen","op der","Aan ‘t","aan ‘t","De las","de las","Van la","van la","Aan de","aan de","op den","Op der","De die","de die","Op den","Aus ‘m","Onder","op ‘t","Op ‘t","Op de","op de","onder","Uit t","Van t","De l’","uit t","Dalla","de l’","De la","de le","De le","de la","Von t","bij t","Am de","aan t","Aan t","am de","Aus m","Bij t","von t","aus m","Degli","dalla","van t","In de","in ‘t","In ‘t","della","in de","unter","Unter","Della","degli","deca","uijt","Over","over","Voor","in t","In t","Uijt","voor","op t","De l","thoe","de l","thor","Deca","Thor","Dal’","Thoe","Op t","dal’","zum","Zur","toe","zur","tot","Toe","Tot","uit","vom","Von","Vom","ver","Ver","van","Van","vor","Vor","Uit","von","Zum","Los","Den","den","del","Del","Dei","dei","Der","der","dos","Het","Dos","don","Des","des","das","Das","Aus","aus","auf","Auf","Aan","aan","Ben","ben","Dal","dal","bin","Bin","Bij","bij","het","Don","Ter","las","ter","ten","Ten","los","les","Les","Tho","Las","tho","d’","‘T","‘t","D’","Da","da","l’","op","S’","‘s","s’","te","af","am","Am","al","Af","de","Al","zu","Zu","to","Te","De","du","El","Du","Le","do","Op","el","la","in","L’","In","im","La","Im","Do","To","lo","di","‘S","Of","Lo","le","of","Di","A","l","S","s","L","I","T","t","D","d","i","a");
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

function spcivipush_extra_check($data, $contact) {
  if (valid_email_address($contact['display_name'])) {
    return TRUE;
  }
  // Check birthdate.
  if (isset($data['birth_date']) && $data['birth_date'] === $contact['birth_date']) {
    return TRUE;
  }

  // Name check.
  $contact['first_name'] = preg_replace('/[^A-Z]/', '', strtoupper($contact['first_name']));
  $contact['last_name'] = preg_replace('/[^A-Z]/', '', strtoupper($contact['last_name']));
  $data['first_name'] = preg_replace('/[^A-Z]/', '', strtoupper($data['first_name']));
  $data['last_name'] = preg_replace('/[^A-Z]/', '', strtoupper($data['last_name']));

  // Check if first and last names are missing in both contact and data.
  if (
    empty($contact['first_name']) &&
    empty($contact['last_name']) &&
    empty($data['first_name']) &&
    empty($data['last_name'])
  ) {
    return TRUE;
  }

  if (
    (
      !empty($contact['first_name']) &&
      !empty($data['first_name'])
    ) ||
    (
      !empty($contact['last_name']) &&
      !empty($data['last_name'])
    )
  ) {
    // Check if complete names match.
    if ($contact['first_name'] . $contact['last_name'] == $data['first_name'] . $data['last_name']) {
      return TRUE;
    }

    // Last name and first letter first name are equal.
    if (
        !empty($data['first_name']) &&
        substr($contact['first_name'], 0, 1) == substr($data['first_name'], 0, 1) &&
        $contact['last_name'] == $data['last_name']
    ) {
      return TRUE;
    }

    // First name or last name contact contains first name data or
    // first name or last name data contains first name contact.
    if (
      strlen($contact['first_name']) > 2 &&
      strlen($data['first_name']) > 2 &&
      (
        strpos($contact['first_name'], $data['first_name']) !== FALSE ||
        strpos($data['first_name'], $contact['first_name']) !== FALSE ||
        strpos($contact['last_name'], $data['first_name']) !== FALSE ||
        strpos($data['last_name'], $contact['first_name']) !== FALSE
      )
    ) {
      return TRUE;
    }
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


function spcivipush_add_notes($existing_contact, $data, &$merge_params, $debug_info) {
  // If available add note.
  if (!empty($data['notes'])) {

    // Get field ids.
    $content_field_id = variable_get('spcivipush_note_content_field');
    $use_field_id = variable_get('spcivipush_note_use_field');
    $author_field_id = variable_get('spcivipush_note_author_field');
    $date_field_id = variable_get('spcivipush_note_date_field');
    $source_field_id = variable_get('spcivipush_note_source_field');

    if (!empty($existing_contact)) {
      if (!empty($existing_contact['api.CustomValue.get']['values'])) {
        foreach ($existing_contact['api.CustomValue.get']['values'] as $field_values) {
          if (isset($field_values['id']) && $field_values['id'] === $source_field_id) {
            foreach ($field_values as $key => $value) {
              if( filter_var($key, FILTER_VALIDATE_INT) ){
                if ($value === $data['source']) {
                  // Note was already added.
                  drupal_set_message('Notitie bestaat reeds.');
                  watchdog('spcivipush', 'Notitie bestaat reeds.');
                  return;
                }
              }
            }
          }
        }
      }
    }

    $date = format_date(time(), 'custom', 'Ymd', date_default_timezone_get());

    foreach ($data['notes'] as $note_data) {
      if (!empty(trim($note_data['note_content']))) {
        foreach($note_data['note_use'] as $note_use) {
          $params[] = array(
            'custom_' . $content_field_id => $note_data['note_content'],
            'custom_' . $use_field_id => $note_use,
            'custom_' . $author_field_id => $data['author'],
            'custom_' . $date_field_id => $date,
            'custom_' . $source_field_id => $data['source'],
          );
        }

        $merge_params = array_merge($merge_params, array('api.CustomValue.create' => $params));
      }
    }
  }
}

function spcivipush_merge_name($existing_contact, $data, $overwrite, &$merge_params, &$merged_data, $debug_info) {
  $contact_id = $existing_contact['id'];
  // check if new name data.
  if (!empty($data['first_name']) || !empty($data['last_name']) || !empty($data['name'])) {

    // Check if name has changed.
    $new_first = empty($data['first_name']) ? '' : $data['first_name'];
    $new_middle = empty($data['middle_name']) ? '' : $data['middle_name'];
    $new_last = empty($data['last_name']) ? '' : $data['last_name'];
    $new_name = trim(preg_replace('!\s+!', ' ', implode(' ', array($new_first, $new_middle, $new_last))));
    $new_name = !empty($new_name) ? $new_name : trim(preg_replace('!\s+!', ' ', $data['name']));
    $new_name = !empty($new_name) ? $new_name : '';
    $new_name_array = explode(' ', $new_name);

    $old_name = empty($existing_contact['display_name']) ? '' : $existing_contact['display_name'];
    $old_name = trim(preg_replace('!\s+!', ' ', $old_name));
    $old_name_array = explode(' ', $old_name);

    // Check if new name is different from old name.
    if (!empty($new_name) && $old_name !== $new_name) {
      // Check if newname is caps locked.
      if (strtoupper($new_name) !== $new_name) {
        // Check if old name contains new name.
        if(strpos($old_name, $new_name) === FALSE) {
          // Check if new name only first letter.
          if (
            !(
              count($new_name_array) > 1 &&
              count($old_name_array) === count($new_name_array) &&
              strtoupper(substr($new_name_array[0], 0, 1)) === strtoupper(substr($old_name_array[0], 0, 1)) &&
              (
                strpos($new_name_array[0], '.') !== FALSE ||
                (
                  strlen(str_replace('.', '', $new_name_array[0])) < 3 &&
                  strlen(str_replace('.', '', $new_name_array[0])) <  strlen(str_replace('.', '', $old_name_array[0]))
                )
              )
            )
          ) {
            // Check if new name can be written without check.
            if (empty($old_name) || $overwrite || valid_email_address($old_name)) {
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
              // Merge params.
              $merge_params = array_merge($merge_params, $params);
              if (!valid_email_address($old_name)) {
                $merged_data['name'] = array(
                  'original' => $existing_contact['display_name'],
                  'new' => $new_name,
                  'changed' => TRUE,
                );
              }
            }
            else {
              // No new name written, create action to check new name.
              $merged_data['name'] = array(
                'original' => $existing_contact['display_name'],
                'new' => $new_name,
                'changed' => FALSE,
              );
            }
          }
        }
      }
    }
  }
}

function spcivipush_merge_email($existing_contact, $data, $overwrite, &$merge_params, &$merged_data, $debug_info) {
  if (!empty($data['email'])) {
    // Check if e-mail is already set.
    if (!in_array($data['email'], array_column($existing_contact['api.Email.get']['values'], 'email'))) {
      if (empty($existing_contact['email']) || $overwrite) {
        $primary = 1;
        $location_type_id = 1;
      }
      else {
        $primary = 0;
        $location_type_id = 1;
      }
      // Add email.
      $params = array(
        'location_type_id' => $location_type_id,
        'email' => $data['email'],
        'is_primary' => $primary,
      );
      $merge_params = array_merge($merge_params, array('api.Email.create' => array($params)));
      $merged_data['email'] = array(
        'email' => $data['email'],
        'primary_text' => $primary ? ' primair' : ' (nog niet ingesteld als primair)',
      );
    }
  }
}

function spcivipush_merge_initials($existing_contact, $data, $overwrite, &$merge_params, &$merged_data, $debug_info) {
  if (!empty($data['initials'])) {
    // Check if birth date is already set.
    if (empty($existing_contact['custom_111']) || $overwrite) {
      // Add email.
      $merge_params['custom_111'] = $data['initials'];
      $merged_data['initials'] = $data['initials'];
    }
  }
}

function spcivipush_merge_birth_date($existing_contact, $data, $overwrite, &$merge_params, &$merged_data, $debug_info) {
  if (!empty($data['birth_date'])) {
    // Check if birth date is already set.
    if (empty($existing_contact['birth_date']) || $overwrite) {
      // Add email.
      $merge_params['birth_date'] = $data['birth_date'];
      $merged_data['birth_date'] = $data['birth_date'];
    }
  }
}

function spcivipush_merge_gender($existing_contact, $data, $overwrite, &$merge_params, &$merged_data, $debug_info) {
  if (!empty($data['gender'])) {
    // Check if birth date is already set.
    if (empty($existing_contact['gender_id']) || $overwrite) {
      // Add gender.
      $merge_params['gender_id'] = $data['gender'];
      switch ($gender) {
        case 1:
        $gender_verbose = 'vrouw';
        break;

        case 2:
        $gender_verbose = 'man';
        break;

        case 3:
        $gender_verbose = 'anders';
        break;
      }
      $merged_data['gender'] = $gender_verbose;
    }
  }
}

function spcivipush_merge_telephone($existing_contact, $data, $overwrite, &$merge_params, &$merged_data, $debug_info) {
  $contact_id = $existing_contact['id'];
  $params = array();
  if (!empty($data['telephone']) && (empty($data['phone_mobile']) || $data['telephone'] !== $data['phone_mobile'])) {
    if (empty($data['phone_mobile'])) {
      $is_primary = 1;
    }
    else {
      $is_primary = 0;
    }
    // Check if phone is already set.
    if (!in_array($data['telephone'], array_column($existing_contact['api.Phone.get']['values'], 'phone_numeric'))) {
      // Phone doesn't exist, add.
      if (empty($existing_contact['phone']) || $overwrite) {
        $primary = $is_primary;
      }
      else {
        $primary = 0;
      }
      if (strpos($data['telephone'], '06') === 0) {
        $phone_type_id = 2;
      }
      else {
        $phone_type_id = 1;
      }
      $params[] = array(
        'contact_id' => $contact_id,
        'location_type_id' => 1,
        'phone' => $data['telephone'],
        'is_primary' => $primary,
        'phone_type_id' => $phone_type_id,
      );
      if (!empty($existing_contact['phone'])) {
        $merged_data['phone'] = array(
          'phone' => $data['telephone'],
          'primary_text' => $primary ? ' primair' : ' (niet ingesteld als primair)',
        );
      }
    }
  }
  if (!empty($data['phone_mobile'])) {
    // Check if phone is already set.
    if (!in_array($data['phone_mobile'], array_column($existing_contact['api.Phone.get']['values'], 'phone_numeric'))) {
      // Phone doesn't exist, add.
      if (empty($existing_contact['phone']) || $overwrite) {
        $primary = 1;
      }
      else {
        $primary = 0;
      }
      $params[] = array(
        'contact_id' => $contact_id,
        'location_type_id' => 1,
        'phone' => $data['phone_mobile'],
        'is_primary' => $primary,
        'phone_type_id' => 2,
      );
      if (!empty($existing_contact['phone'])) {
        $merged_data['phone_mobile'] = array(
          'phone_mobile' => $data['phone_mobile'],
          'primary_text' => $primary ? ' primair' : ' (niet ingesteld als primair)',
        );
      }
    }
  }
  if (!empty($params)) {
    $merge_params = array_merge($merge_params, array('api.Phone.create' => $params));
  }
}

function spcivipush_merge_address($existing_contact, $data, $overwrite, &$merge_params, &$merged_data, $debug_info) {
  $contact_id = $existing_contact['id'];
  $spcivi = \SPCivi::getInstance();
  // Get country ID.
  $country_ids = spcivipush_get_country_ids();
  if (isset($data['country'])) {
    $country_id = array_search($data['country'], $country_ids);
  }
  // Get contact primary address.
  $primary_address = array();
  $primary_address_key = array_search(1, array_column($existing_contact['api.Address.get']['values'], 'is_primary'));
  if ($primary_address_key !== FALSE) {
    $primary_address = $existing_contact['api.Address.get']['values'][$primary_address_key];
  }

  // Check if primary address should be added.
  if (
    $overwrite ||
    empty($primary_address) ||
    (
      empty($existing_contact['street_address']) &&
      empty($existing_contact['house_number']) &&
      (
        (!empty($data['house_number']) && !empty($data['postal_code'])) ||
        (!empty($data['street_and_number']) && !empty($data['locality']))
      )
    )
  ) {

    // Check contact home address already exists.
    $add_new_primary_address = FALSE;
    if (!empty($primary_address)) {
      // check if address is different.
      $different_address = FALSE;
      if (
        // Existing address not specific to house, and specific address data.
        (
          (!empty($data['house_number']) || !empty($data['street_address'])) &&
          empty($primary_address['street_number']) &&
          empty($primary_address['street_address'])
        ) ||
        // Existing address house number differs form address data.
        (
          !empty($data['house_number']) &&
          !empty($primary_address['street_number']) &&
          $data['house_number'] !== $primary_address['street_number']
        ) ||
        // Existing full address differs from data address.
        (
          !empty($data['street_and_number']) &&
          !empty($primary_address['street_address']) &&
          $data['street_and_number'] !== $primary_address['street_address']
        ) ||
        // Existing house number addition differs from data house number addition.
        (
          !empty($data['house_number_addition']) &&
          !empty($primary_address['street_unit']) &&
          $data['house_number_addition'] !== $primary_address['street_unit']
        ) ||
        // No existing house number addition, but addition in data.
        (
          !empty($data['house_number_addition']) &&
          empty($primary_address['street_unit'])
        )
      ) {
        $add_new_primary_address = TRUE;
      }
    }
    else {
      // No existing home address found.
      $add_new_primary_address = TRUE;
    }
    if ($add_new_primary_address) {
      // Add new address.
      $autosync = array(
        'street_and_number' => 'street_address',
        'street' => 'street_name',
        'house_number' => 'street_number',
        'house_number_addition' => 'street_unit',
        'postal_code' => 'postal_code',
        'locality' => 'city',
      );
      $params = array(
        'location_type_id' => 1,
        'is_primary' => 1,
        'is_billing' => 1,
      );

      if (empty($country_id)) {
        $params['country_id'] = 1152;
      }
      else {
        $params['country_id'] = $country_id;
      }
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
        // Store merged data.
        $merge_params = array_merge($merge_params, array('api.Address.create' => array($params)));
        if (!empty($existing_contact['address_id'])) {
          foreach ($address_merge_fields as $key => $value) {
            $merged_data['address'][$key] = $value;
          };
          $merged_data['address']['primary_text'] = ' primair';
        }
      }
    }
  }
  else {
    // No overwrite, check if comparable address exists, then do nothing, else add new address.

    $comparable_address = array();

    // Check if address with same house number exists.
    $existing_house_numbers = array_column($existing_contact['api.Address.get']['values'], 'street_number');
    if (!empty($data['house_number']) && !empty($existing_house_numbers)) {
      if (in_array($data['house_number'], $existing_house_numbers)) {
        $comparable_address['house_number'] = 'house_number';
      }
    }

    // Check if address with same postal code exists.
    $existing_postal_codes = array_column($existing_contact['api.Address.get']['values'], 'postal_code');
    if (!empty($data['postal_code']) && !empty($existing_postal_codes)) {
      if (in_array($data['postal_code'], $existing_postal_codes)) {
        $comparable_address['postal_code'] = 'postal_code';
      }
    }

    // Check if address with same street and number exists.
    $existing_street_and_numbers = array_column($existing_contact['api.Address.get']['values'], 'street_address');
    if (!empty($data['street_and_number']) && !empty($existing_street_and_numbers)) {
      if (in_array($data['street_and_number'], $existing_street_and_numbers)) {
        $comparable_address['street_and_number'] = 'street_and_number';
      }
    }

    if (empty($comparable_address)) {
      // Check in sufficient new data.
      if (
        // No existing address.
        empty($existing_contact['address_id']) ||
        (
          // New data specific.
          (!empty($data['house_number']) && !empty($data['postal_code'])) ||
          (!empty($data['street_and_number']) && !empty($data['locality']))
        ) ||
        (
          // Existing data less specific.
          (empty($data['house_number']) && empty($data['postal_code'])) &&
          (empty($data['street_and_number']) && empty($data['locality'])) &&
          empty($existing_contact['street_address']) &&
          empty($existing_contact['house_number'])
        )
      ) {
        // Sufficient new data.
        if (empty($existing_contact['address_id'])) {
          // Add primary address.
          $primary = 1;
          $billing = 1;
          $location_type_id = 1;
        }
        else {
          $primary = 0;
          $billing = 0;
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
          'location_type_id' => $location_type_id,
          'is_primary' => $primary,
          'is_billing' => $billing,
        );
        if (empty($country_id)) {
          $params['country_id'] = 1152;
        }
        else {
          $params['country_id'] = $country_id;
        }
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
          $merge_params = array_merge($merge_params, array('api.Address.create' => array($params)));
          if (!empty($existing_contact['address_id'])) {
            foreach ($address_merge_fields as $key => $value) {
              $merged_data['address'][$key] = $value;
            };
            $merged_data['address']['primary_text'] = $primary ? ' primair' : ' (nog niet ingesteld als primair)';
          }
        }
      }
    }
  }
}

function spcivipush_merge_create_activities($existing_contact, $data, $overwrite, $merged_data, $debug_info) {
  $contact_id = $existing_contact['id'];
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
    if (!empty($merged_data['initials'])) {
      $text .= '<p>Initialen zijn toegevoegd: ' . check_plain($merged_data['initials']) . '.</p>';
    }
    if (!empty($merged_data['birth_date'])) {
      $text .= '<p>Geboortedatum is toegevoegd: ' . check_plain($merged_data['birth_date']) . '.</p>';
    }
    if (!empty($merged_data['gender'])) {
      $text .= '<p>Geslacht informatie is toegevoegd: ' . check_plain($merged_data['gender']) . '.</p>';
    }
    if (
      !empty($merged_data['email']) ||
      !empty($merged_data['phone']) ||
      !empty($merged_data['phone_mobile']) ||
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
      if (!empty($merged_data['phone_mobile'])) {
        $text .= '<p>mobiel telefoonnummer' . $merged_data['phone_mobile']['primary_text'] . ': ' . check_plain($merged_data['phone_mobile']['phone_mobile']) . "</p>";
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
    drupal_set_message('Contactinformatie in CiviCRM samengevoegd met bestaand contact: <a href="https://spnet.nl/civicrm/contact/view?reset=1&cid=' . $contact_id . '" target="_blank">' . $contact_id . '</a>');
    watchdog('spcivipush', 'Contactinformatie in CiviCRM samengevoegd met bestaand contact: ' . $contact_id);
  }
}

function spcivipush_get_country_ids() {
  $codes = &drupal_static(__FUNCTION__);
  if (!isset($codes)) {
    if ($cache = cache_get('spcivipush_country_codes')) {
      $codes = $cache->data;
    }
    else {
      // Do your expensive calculations here, and populate $my_data
      // with the correct stuff..
      $api = spciviapi_get_api();
      $api->Constant->Get([
        'name'       => 'countryIsoCode',
        'sequential' => 0,
      ]);
      $codes_raw = $api->result();
      if ($codes_raw) {
        $codes = get_object_vars($codes_raw->values);
        cache_set('spcivipush_country_codes', $codes, 'cache', time() + 86400);
      }
    }
  }
  return $codes;
}
