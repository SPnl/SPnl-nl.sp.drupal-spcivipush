<?php

function spcivipush_settings_form($form, &$form_state) {

  // Civicrm settings.
  $form['spcivipush_civicrm_settings'] = array(
    '#type' => 'fieldset',
    '#title' => 'CiviCRM synchronisatie instellingen',
  );

  $custom_fields = spcivipush_get_civi_custom_fields();

  $form['spcivipush_note_settings'] = array(
    '#type' => 'fieldset',
    '#title' => 'CiviCRM notitie velden',
  );

  $form['spcivipush_note_settings']['spcivipush_note_content_field'] = array(
    '#type' => 'select',
    '#title' => 'Inhoud:',
    '#options' => $custom_fields,
    '#empty_option' => '- Geen -',
    '#default_value' => variable_get('spcivipush_note_content_field', array()),
  );

  $form['spcivipush_note_settings']['spcivipush_note_use_field'] = array(
    '#type' => 'select',
    '#title' => 'Gebruik voor:',
    '#options' => $custom_fields,
    '#empty_option' => '- Geen -',
    '#default_value' => variable_get('spcivipush_note_use_field', array()),
  );

  $form['spcivipush_note_settings']['spcivipush_note_author_field'] = array(
    '#type' => 'select',
    '#title' => 'Auteur:',
    '#options' => $custom_fields,
    '#empty_option' => '- Geen -',
    '#default_value' => variable_get('spcivipush_note_author_field', array()),
  );

  $form['spcivipush_note_settings']['spcivipush_note_date_field'] = array(
    '#type' => 'select',
    '#title' => 'Datum:',
    '#options' => $custom_fields,
    '#empty_option' => '- Geen -',
    '#default_value' => variable_get('spcivipush_note_date_field', array()),
  );

  $form['spcivipush_note_settings']['spcivipush_note_source_field'] = array(
    '#type' => 'select',
    '#title' => 'Bron:',
    '#options' => $custom_fields,
    '#empty_option' => '- Geen -',
    '#default_value' => variable_get('spcivipush_note_source_field', array()),
  );

  return system_settings_form($form);
}

function spcivipush_settings_form_validate($form, &$form_state) {
  $values = $form_state['values'];
}

function spcivipush_get_civi_custom_fields($sync = TRUE) {
  $custom_fields = &drupal_static(__FUNCTION__);
  if (!isset($custom_fields)) {

    $custom_fields = array();
    set_time_limit(0);
    $spcivi = \SPCivi::getInstance();
    $params = array(
      'sequential'            => 1,
      'option.limit'          => 9999,
      'is_active' => 1,
    );
    $custom_fields_results = $spcivi->api('CustomField', 'get', $params);
    if (_spcivipush_check_result($custom_fields_results, 'custom_fields', 'get', $params)) {
      if (!empty($custom_fields_results['values'])) {
        foreach ($custom_fields_results['values'] as $value) {
          $custom_fields[$value['id']] = $value['label'] . ' (' . $value['id'] . ')';
        }
        asort($custom_fields);
      }
      else {
        drupal_set_message('Error syncing civicrm custom_fields, using saved data.', 'warning');
      }
    }
  }
  return $custom_fields;
}
