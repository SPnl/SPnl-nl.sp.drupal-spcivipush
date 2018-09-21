<?php

function spcivipush_settings_form($form, &$form_state) {

  // Civicrm settings.
  $form['spcivipush_civicrm_settings'] = array(
    '#type' => 'fieldset',
    '#title' => 'CiviCRM synchronisatie instellingen',
  );
  $tags = spcivipush_get_civi_tags();
  if (empty(variable_get('spcivipush_unverified_tag', FALSE)) && $id = array_search('Ongecontroleerd', $tags)) {
    variable_set('spcivipush_unverified_tag', $id);
  }
  $form['spcivipush_civicrm_settings']['spcivipush_unverified_tag'] = array(
    '#type' => 'select',
    '#title' => 'Kies de tag die moet worden toegevoegd aan contacten die handmatig moeten worden gecontroleerd.',
    '#options' => $tags,
    '#empty_option' => '- Geen -',
    '#default_value' => variable_get('spcivipush_unverified_tag', array()),
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

function spcivipush_get_civi_tags($sync = TRUE) {
  if ($sync) {
    $tags = &drupal_static(__FUNCTION__);
    if (!isset($tags)) {

      if ($cache = cache_get('spcivipush_civi_tags')) {
        $tags = $cache->data;
      }
      else {
        $tags = array();
        set_time_limit(0);
        $spcivi = \SPCivi::getInstance();
        $params = array(
          'sequential'            => 1,
          'option.limit'          => 9999,
          'is_selectable' => 1,
          'used_for' => 'civicrm_contact',
        );
        $tags_results = $spcivi->api('Tag', 'get', $params);
        if (_spcivipush_check_result($tags_results, 'tags', 'get', $params)) {
          if (!empty($tags_results['values'])) {
            foreach ($tags_results['values'] as $value) {
              $tags[$value['id']] = $value['name'];
            }
            asort($tags);
            variable_set('spcivipush_civi_tags', $tags);
          }
          else {
            drupal_set_message('Error syncing civicrm tags, using saved data.', 'warning');
          }
        }
      }
      $tags = variable_get('spcivipush_civi_tags', array());
      cache_set('spcivipush_civi_tags', $tags, 'cache', time() + 3600);
    }
    return $tags;
  }
  $tags = variable_get('spcivipush_civi_tags', array());
  return $tags;
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
